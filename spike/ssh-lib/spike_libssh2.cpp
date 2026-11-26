// M0 spike: libssh2 (1.11) against a real target.
#include "common.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <libssh2.h>

#include <string>
#include <thread>

static SOCKET Dial(const Args &a)
{
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(a.host.c_str(), std::to_string(a.port).c_str(), &hints, &res) != 0)
        return INVALID_SOCKET;
    SOCKET sock = INVALID_SOCKET;
    for (auto *p = res; p; p = p->ai_next)
    {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock != INVALID_SOCKET && connect(sock, p->ai_addr, (int)p->ai_addrlen) == 0)
            break;
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    return sock;
}

static std::string LastError(LIBSSH2_SESSION *s)
{
    char *msg = nullptr;
    int code = libssh2_session_last_error(s, &msg, nullptr, 0);
    return "err=" + std::to_string(code) + " " + (msg ? msg : "");
}

struct Conn
{
    SOCKET sock = INVALID_SOCKET;
    LIBSSH2_SESSION *s = nullptr;
    bool Open(const Args &a)
    {
        sock = Dial(a);
        if (sock == INVALID_SOCKET)
            return false;
        s = libssh2_session_init();
        return libssh2_session_handshake(s, (libssh2_socket_t)sock) == 0;
    }
    ~Conn()
    {
        if (s)
        {
            libssh2_session_disconnect(s, "bye");
            libssh2_session_free(s);
        }
        if (sock != INVALID_SOCKET)
            closesocket(sock);
    }
};

static std::string ReadFor(LIBSSH2_CHANNEL *ch, int ms)
{
    std::string out;
    char buf[4096];
    Stopwatch sw;
    while (sw.Ms() < ms)
    {
        ssize_t n = libssh2_channel_read(ch, buf, sizeof(buf));
        if (n > 0)
            out.append(buf, n);
        else if (n == LIBSSH2_ERROR_EAGAIN)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        else if (n < 0 || libssh2_channel_eof(ch))
            break;
    }
    return out;
}

static void ListSupported(LIBSSH2_SESSION *s, int method, const char *label, const char *want)
{
    const char **algs = nullptr;
    int n = libssh2_session_supported_algs(s, method, &algs);
    bool found = false;
    for (int i = 0; i < n; i++)
        if (std::strcmp(algs[i], want) == 0)
            found = true;
    if (n > 0)
        libssh2_free(s, algs);
    Report(found, label, found ? "supported" : "not in supported list");
}

int main(int argc, char **argv)
{
    Args a = ParseArgs(argc, argv);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    libssh2_init(0);
    Info("library", std::string("libssh2 ") + libssh2_version(0));
    Info("parse ~/.ssh/config", "not available in libssh2 (would be our code)");

    // --- Session 1: handshake info, host key, auth methods, agent auth, PTY ---
    {
        Conn c;
        Stopwatch sw;
        if (!c.Open(a))
        {
            Report(false, "connect", c.s ? LastError(c.s) : "tcp connect failed");
            return 1;
        }
        Report(true, "connect", std::to_string(sw.Ms()) + " ms");
        Info("kex", libssh2_session_methods(c.s, LIBSSH2_METHOD_KEX));
        Info("cipher in / out", std::string(libssh2_session_methods(c.s, LIBSSH2_METHOD_CRYPT_SC)) + " / " +
                                    libssh2_session_methods(c.s, LIBSSH2_METHOD_CRYPT_CS));
        Info("mac in", libssh2_session_methods(c.s, LIBSSH2_METHOD_MAC_SC));

        const unsigned char *h = (const unsigned char *)libssh2_hostkey_hash(c.s, LIBSSH2_HOSTKEY_HASH_SHA256);
        Info("host key", std::string(libssh2_session_methods(c.s, LIBSSH2_METHOD_HOSTKEY)) + " SHA256:" + (h ? Base64NoPad(h, 32) : "?"));

        LIBSSH2_KNOWNHOSTS *nh = libssh2_knownhost_init(c.s);
        std::string khPath = HomeDir() + "\\.ssh\\known_hosts";
        int nread = libssh2_knownhost_readfile(nh, khPath.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH);
        size_t klen = 0;
        int ktype = 0;
        const char *key = libssh2_session_hostkey(c.s, &klen, &ktype);
        libssh2_knownhost *found = nullptr;
        int kc = libssh2_knownhost_checkp(nh, a.host.c_str(), a.port, key, klen,
                                          LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW, &found);
        Report(kc == LIBSSH2_KNOWNHOST_CHECK_MATCH, "known_hosts check",
               "entries read=" + std::to_string(nread) + " result=" + std::to_string(kc) + " (0=match,1=mismatch,2=notfound)");
        libssh2_knownhost_free(nh);

        char *list = libssh2_userauth_list(c.s, a.user.c_str(), (unsigned)a.user.size());
        Info("server auth methods", list ? list : "(none)");

        sw = Stopwatch();
        LIBSSH2_AGENT *agent = libssh2_agent_init(c.s);
        bool authed = false;
        std::string agentDetail;
        if (libssh2_agent_connect(agent) != 0)
            agentDetail = "connect failed: " + LastError(c.s);
        else if (libssh2_agent_list_identities(agent) != 0)
            agentDetail = "list identities failed: " + LastError(c.s);
        else
        {
            libssh2_agent_publickey *id = nullptr, *prev = nullptr;
            int count = 0;
            while (libssh2_agent_get_identity(agent, &id, prev) == 0)
            {
                count++;
                if (libssh2_agent_userauth(agent, a.user.c_str(), id) == 0)
                {
                    authed = true;
                    break;
                }
                prev = id;
            }
            agentDetail = std::to_string(count) + " identities tried, " + std::to_string(sw.Ms()) + " ms";
        }
        Report(authed, "auth: agent", agentDetail);
        libssh2_agent_disconnect(agent);
        libssh2_agent_free(agent);

        if (authed)
        {
            LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(c.s);
            bool ok = ch && libssh2_channel_request_pty_ex(ch, "xterm-256color", 14, nullptr, 0, 80, 24, 0, 0) == 0 &&
                      libssh2_channel_exec(ch, kPtyCmd) == 0;
            Report(ok, "pty + exec", ok ? "xterm-256color 80x24" : LastError(c.s));
            if (ok)
            {
                libssh2_session_set_blocking(c.s, 0);
                std::string out1 = ReadFor(ch, 1000);
                int rz;
                while ((rz = libssh2_channel_request_pty_size(ch, 120, 40)) == LIBSSH2_ERROR_EAGAIN)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                Report(rz == 0, "window-change 120x40", rz == 0 ? "sent" : LastError(c.s));
                std::string out2 = ReadFor(ch, 4000);
                Info("pty output", Printable(out1 + out2));
                Report(out1.find("24 80") != std::string::npos, "stty size before", "expect 24 80");
                Report(out2.find("40 120") != std::string::npos, "stty size after resize", "expect 40 120");
                Report(out2.find("\xE2\x9C\x93 utf8") != std::string::npos, "utf-8 passthrough", "expect check mark");
                libssh2_session_set_blocking(c.s, 1);
                libssh2_channel_close(ch);
                libssh2_channel_wait_closed(ch);
                int es = libssh2_channel_get_exit_status(ch);
                Report(es == 7, "remote exit status", std::to_string(es) + " (expect 7)");
                libssh2_channel_free(ch);
            }
        }
    }

    // --- Session 2: private key file (no agent) ---
    {
        Conn c;
        if (c.Open(a))
        {
            int rc = libssh2_userauth_publickey_fromfile_ex(c.s, a.user.c_str(), (unsigned)a.user.size(),
                                                            nullptr, a.keyFile.c_str(), nullptr);
            Report(rc == 0, "auth: key file", rc == 0 ? "ok" : LastError(c.s));
        }
    }

    // --- Supported legacy algorithms ---
    {
        LIBSSH2_SESSION *s = libssh2_session_init();
        ListSupported(s, LIBSSH2_METHOD_KEX, "legacy kex group14-sha1", "diffie-hellman-group14-sha1");
        ListSupported(s, LIBSSH2_METHOD_KEX, "legacy kex group1-sha1", "diffie-hellman-group1-sha1");
        ListSupported(s, LIBSSH2_METHOD_HOSTKEY, "legacy hostkey ssh-rsa", "ssh-rsa");
        ListSupported(s, LIBSSH2_METHOD_CRYPT_CS, "legacy cipher aes128-cbc", "aes128-cbc");
        ListSupported(s, LIBSSH2_METHOD_KEX, "kex curve25519-sha256", "curve25519-sha256");
        ListSupported(s, LIBSSH2_METHOD_CRYPT_CS, "cipher chacha20-poly1305", "chacha20-poly1305@openssh.com");
        libssh2_session_free(s);
    }

    libssh2_exit();
    WSACleanup();
    return 0;
}
