// M0 spike: libssh (0.12) against a real target.
#include "common.h"

#include <libssh/libssh.h>

#include <string>

static ssh_session NewSession(const Args &a, bool parseConfig)
{
    ssh_session s = ssh_new();
    ssh_options_set(s, SSH_OPTIONS_HOST, a.host.c_str());
    ssh_options_set(s, SSH_OPTIONS_USER, a.user.c_str());
    ssh_options_set(s, SSH_OPTIONS_PORT, &a.port);
    if (parseConfig)
    {
        int rc = ssh_options_parse_config(s, nullptr); // ~/.ssh/config
        Report(rc == SSH_OK, "parse ~/.ssh/config", rc == SSH_OK ? "ok" : ssh_get_error(s));
    }
    return s;
}

static std::string ReadFor(ssh_channel ch, int ms)
{
    std::string out;
    char buf[4096];
    Stopwatch sw;
    while (sw.Ms() < ms)
    {
        int n = ssh_channel_read_timeout(ch, buf, sizeof(buf), 0, 100);
        if (n > 0)
            out.append(buf, n);
        else if (n == SSH_AGAIN) // 0.12 returns SSH_AGAIN (not 0) when the timeout expires
            continue;
        else if (n < 0 || ssh_channel_is_eof(ch))
        {
            if (std::getenv("SPIKE_DEBUG"))
                std::printf("[DBG ] read n=%d eof=%d open=%d err=%s\n", n, ssh_channel_is_eof(ch),
                            ssh_channel_is_open(ch), ssh_get_error(ssh_channel_get_session(ch)));
            break;
        }
    }
    return out;
}

static void CheckLegacyAlgorithms(const Args &a)
{
    // ssh_options_set rejects algorithm lists with no supported entry.
    ssh_session s = NewSession(a, false);
    int rc = ssh_options_set(s, SSH_OPTIONS_KEY_EXCHANGE, "diffie-hellman-group14-sha1");
    Report(rc == SSH_OK, "legacy kex group14-sha1", rc == SSH_OK ? "accepted" : ssh_get_error(s));
    rc = ssh_options_set(s, SSH_OPTIONS_KEY_EXCHANGE, "diffie-hellman-group1-sha1");
    Report(rc == SSH_OK, "legacy kex group1-sha1", rc == SSH_OK ? "accepted" : ssh_get_error(s));
    rc = ssh_options_set(s, SSH_OPTIONS_HOSTKEYS, "ssh-rsa");
    Report(rc == SSH_OK, "legacy hostkey ssh-rsa", rc == SSH_OK ? "accepted" : ssh_get_error(s));
    rc = ssh_options_set(s, SSH_OPTIONS_CIPHERS_C_S, "aes128-cbc");
    Report(rc == SSH_OK, "legacy cipher aes128-cbc", rc == SSH_OK ? "accepted" : ssh_get_error(s));
    ssh_free(s);
}

int main(int argc, char **argv)
{
    Args a = ParseArgs(argc, argv);
    ssh_init();
    Info("library", std::string("libssh ") + ssh_version(0));

    // --- Session 1: handshake info, host key, auth methods, agent auth, PTY ---
    ssh_session s = NewSession(a, true);
    Stopwatch sw;
    if (ssh_connect(s) != SSH_OK)
    {
        Report(false, "connect", ssh_get_error(s));
        return 1;
    }
    Report(true, "connect", std::to_string(sw.Ms()) + " ms");
    Info("kex", ssh_get_kex_algo(s));
    Info("cipher in / out", std::string(ssh_get_cipher_in(s)) + " / " + ssh_get_cipher_out(s));
    Info("mac in", ssh_get_hmac_in(s) ? ssh_get_hmac_in(s) : "(implicit)");

    ssh_key srvKey = nullptr;
    if (ssh_get_server_publickey(s, &srvKey) == SSH_OK)
    {
        unsigned char *hash = nullptr;
        size_t hlen = 0;
        ssh_get_publickey_hash(srvKey, SSH_PUBLICKEY_HASH_SHA256, &hash, &hlen);
        char *fp = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hlen);
        Info("host key", std::string(ssh_key_type_to_char(ssh_key_type(srvKey))) + " " + fp);
        ssh_string_free_char(fp);
        ssh_clean_pubkey_hash(&hash);
        ssh_key_free(srvKey);
    }
    enum ssh_known_hosts_e kh = ssh_session_is_known_server(s);
    Report(kh == SSH_KNOWN_HOSTS_OK, "known_hosts check",
           kh == SSH_KNOWN_HOSTS_OK ? "match" : "state=" + std::to_string(kh));

    ssh_userauth_none(s, nullptr);
    int methods = ssh_userauth_list(s, nullptr);
    std::string m;
    if (methods & SSH_AUTH_METHOD_PASSWORD) m += "password ";
    if (methods & SSH_AUTH_METHOD_PUBLICKEY) m += "publickey ";
    if (methods & SSH_AUTH_METHOD_INTERACTIVE) m += "keyboard-interactive ";
    Info("server auth methods", m);

    sw = Stopwatch();
    int rc = ssh_userauth_agent(s, nullptr);
    Report(rc == SSH_AUTH_SUCCESS, "auth: agent", rc == SSH_AUTH_SUCCESS ? std::to_string(sw.Ms()) + " ms" : std::string("rc=") + std::to_string(rc) + " " + ssh_get_error(s));
    if (rc != SSH_AUTH_SUCCESS)
    {
        rc = ssh_userauth_publickey_auto(s, nullptr, nullptr);
        Report(rc == SSH_AUTH_SUCCESS, "auth: publickey_auto", rc == SSH_AUTH_SUCCESS ? "ok" : ssh_get_error(s));
    }

    if (rc == SSH_AUTH_SUCCESS)
    {
        ssh_channel ch = ssh_channel_new(s);
        bool ok = ssh_channel_open_session(ch) == SSH_OK &&
                  ssh_channel_request_pty_size(ch, "xterm-256color", 80, 24) == SSH_OK &&
                  ssh_channel_request_exec(ch, kPtyCmd) == SSH_OK;
        Report(ok, "pty + exec", ok ? "xterm-256color 80x24" : ssh_get_error(s));
        if (ok)
        {
            std::string out1 = ReadFor(ch, 1000);
            int rz = ssh_channel_change_pty_size(ch, 120, 40);
            Report(rz == SSH_OK, "window-change 120x40", rz == SSH_OK ? "sent" : ssh_get_error(s));
            std::string out2 = ReadFor(ch, 4000);
            Info("pty output", Printable(out1 + out2));
            Report(out1.find("24 80") != std::string::npos, "stty size before", "expect 24 80");
            Report(out2.find("40 120") != std::string::npos, "stty size after resize", "expect 40 120");
            Report(out2.find("\xE2\x9C\x93 utf8") != std::string::npos, "utf-8 passthrough", "expect check mark");
            ssh_channel_send_eof(ch);
            ssh_channel_close(ch);
            uint32_t es = 0;
            char *sig = nullptr;
            int core = 0;
            ssh_channel_get_exit_state(ch, &es, &sig, &core);
            ssh_string_free_char(sig);
            Report(es == 7, "remote exit status", std::to_string(es) + " (expect 7)");
        }
        ssh_channel_free(ch);
    }
    Info("socket fd for asio", std::to_string((long long)ssh_get_fd(s)));
    ssh_disconnect(s);
    ssh_free(s);

    // --- Session 2: private key file (no agent) ---
    s = NewSession(a, false);
    if (ssh_connect(s) == SSH_OK)
    {
        ssh_key key = nullptr;
        int irc = ssh_pki_import_privkey_file(a.keyFile.c_str(), nullptr, nullptr, nullptr, &key);
        if (irc != SSH_OK)
            Report(false, "load key file", "import failed (passphrase-protected or unsupported format?)");
        else
        {
            rc = ssh_userauth_publickey(s, nullptr, key);
            Report(rc == SSH_AUTH_SUCCESS, "auth: key file", rc == SSH_AUTH_SUCCESS ? "ok" : ssh_get_error(s));
            ssh_key_free(key);
        }
        ssh_disconnect(s);
    }
    ssh_free(s);

    CheckLegacyAlgorithms(a);
    ssh_finalize();
    return 0;
}
