#include "ssh/Auth.h"

#include "util/Paths.h"
#include "util/Utf8.h"

#include <windows.h>

#include <libssh2.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace winssh::ssh
{
namespace fs = std::filesystem;

namespace
{

void Wipe(std::string &s)
{
    if (!s.empty())
        ::SecureZeroMemory(s.data(), s.size());
    s.clear();
}

bool Offers(const std::string &methods, const char *name)
{
    // methods is a comma-separated list, e.g. "publickey,password"
    size_t pos = 0;
    const size_t len = std::strlen(name);
    while (pos <= methods.size())
    {
        size_t end = methods.find(',', pos);
        if (end == std::string::npos)
            end = methods.size();
        if (end - pos == len && methods.compare(pos, len, name) == 0)
            return true;
        pos = end + 1;
    }
    return false;
}

bool FileExists(const std::string &utf8)
{
    std::error_code ec;
    return fs::is_regular_file(fs::path(util::Utf8ToWide(utf8)), ec);
}

bool TryAgent(LIBSSH2_SESSION *s, const std::string &user)
{
    LIBSSH2_AGENT *agent = libssh2_agent_init(s);
    if (!agent)
        return false;
    bool ok = false;
    if (libssh2_agent_connect(agent) == 0 && libssh2_agent_list_identities(agent) == 0)
    {
        libssh2_agent_publickey *id = nullptr;
        libssh2_agent_publickey *prev = nullptr;
        while (libssh2_agent_get_identity(agent, &id, prev) == 0)
        {
            if (libssh2_agent_userauth(agent, user.c_str(), id) == 0)
            {
                ok = true;
                break;
            }
            prev = id;
        }
        libssh2_agent_disconnect(agent);
    }
    libssh2_agent_free(agent);
    return ok;
}

// Returns true on success, false when the server rejects the key or it cannot be used.
// Sets *cancelled when the user cancels the passphrase prompt.
bool TryKeyFile(LIBSSH2_SESSION *s, const std::string &user, const std::string &keyFile, const Prompter &prompt,
                bool *cancelled)
{
    // With the .pub file the server is asked first, so we only prompt for a passphrase for keys it accepts.
    const std::string pub = keyFile + ".pub";
    const char *pubPath = FileExists(pub) ? pub.c_str() : nullptr;
    const auto ulen = static_cast<unsigned int>(user.size());

    int rc = libssh2_userauth_publickey_fromfile_ex(s, user.c_str(), ulen, pubPath, keyFile.c_str(), nullptr);
    if (rc == 0)
        return true;
    if (rc != LIBSSH2_ERROR_FILE)
        return false; // rejected by the server, or another error: try the next method

    // LIBSSH2_ERROR_FILE: most likely an encrypted key. Ask for the passphrase.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        auto pass = prompt("Enter passphrase for key '" + keyFile + "': ", false);
        if (!pass)
        {
            *cancelled = true;
            return false;
        }
        if (pass->empty())
            return false; // empty answer = skip this key, like OpenSSH
        rc = libssh2_userauth_publickey_fromfile_ex(s, user.c_str(), ulen, pubPath, keyFile.c_str(), pass->c_str());
        Wipe(*pass);
        if (rc == 0)
            return true;
        if (rc != LIBSSH2_ERROR_FILE)
            return false;
    }
    return false;
}

struct KbdContext
{
    const Prompter *prompt = nullptr;
    bool cancelled = false;
};

KbdContext *g_kbd = nullptr; // keyboard-interactive callback has no user pointer we control

void KbdCallback(const char *name, int nameLen, const char *instruction, int instructionLen, int numPrompts,
                 const LIBSSH2_USERAUTH_KBDINT_PROMPT *prompts, LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses,
                 void **abstract)
{
    (void)abstract;
    KbdContext *ctx = g_kbd;
    if (!ctx)
        return;
    std::string header;
    if (nameLen > 0)
        header.append(name, static_cast<size_t>(nameLen)).append("\r\n");
    if (instructionLen > 0)
        header.append(instruction, static_cast<size_t>(instructionLen)).append("\r\n");
    bool headerShown = header.empty();

    for (int i = 0; i < numPrompts; ++i)
    {
        responses[i].text = nullptr;
        responses[i].length = 0;
        if (ctx->cancelled)
            continue;
        std::string text(reinterpret_cast<const char *>(prompts[i].text), prompts[i].length);
        if (!headerShown)
        {
            text = header + text;
            headerShown = true;
        }
        auto answer = (*ctx->prompt)(text, prompts[i].echo != 0);
        if (!answer)
        {
            ctx->cancelled = true;
            continue;
        }
        // libssh2 frees responses with its default free().
        char *copy = static_cast<char *>(std::malloc(answer->size() + 1));
        if (copy)
        {
            std::memcpy(copy, answer->c_str(), answer->size() + 1);
            responses[i].text = copy;
            responses[i].length = static_cast<unsigned int>(answer->size());
        }
        Wipe(*answer);
    }
}

} // namespace

std::vector<std::string> DefaultKeyFiles()
{
    std::vector<std::string> out;
    for (const wchar_t *name : {L"id_ed25519", L"id_ecdsa", L"id_rsa"})
    {
        const fs::path p = util::SshDir() / name;
        std::error_code ec;
        if (fs::is_regular_file(p, ec))
            out.push_back(util::WideToUtf8(p.wstring()));
    }
    return out;
}

AuthResult Authenticate(LIBSSH2_SESSION *s, const AuthOptions &o, const Prompter &prompt, const Notifier &notify)
{
    AuthResult r;
    const auto ulen = static_cast<unsigned int>(o.user.size());

    const char *list = libssh2_userauth_list(s, o.user.c_str(), ulen);
    if (!list)
    {
        if (libssh2_userauth_authenticated(s))
        {
            r.ok = true;
            r.method = "none";
        }
        return r;
    }
    r.serverMethods = list;

    if (Offers(r.serverMethods, "publickey"))
    {
        if (o.useAgent && TryAgent(s, o.user))
        {
            r.ok = true;
            r.method = "publickey (agent)";
            return r;
        }
        std::vector<std::string> files;
        if (!o.identityFile.empty())
            files.push_back(o.identityFile);
        else
            files = o.keyFiles;
        for (const auto &f : files)
        {
            if (!FileExists(f))
                continue;
            if (TryKeyFile(s, o.user, f, prompt, &r.cancelled))
            {
                r.ok = true;
                r.method = "publickey (" + f + ")";
                return r;
            }
            if (r.cancelled)
                return r;
        }
    }

    if (Offers(r.serverMethods, "keyboard-interactive"))
    {
        KbdContext ctx;
        ctx.prompt = &prompt;
        g_kbd = &ctx;
        for (int attempt = 0; attempt < o.maxPasswordTries && !ctx.cancelled; ++attempt)
        {
            if (libssh2_userauth_keyboard_interactive_ex(s, o.user.c_str(), ulen, KbdCallback) == 0)
            {
                g_kbd = nullptr;
                r.ok = true;
                r.method = "keyboard-interactive";
                return r;
            }
            if (!ctx.cancelled && attempt + 1 < o.maxPasswordTries && notify)
                notify("Permission denied, please try again.\r\n");
        }
        g_kbd = nullptr;
        if (ctx.cancelled)
        {
            r.cancelled = true;
            return r;
        }
    }

    if (Offers(r.serverMethods, "password"))
    {
        for (int attempt = 0; attempt < o.maxPasswordTries; ++attempt)
        {
            auto pass = prompt(o.user + "@" + o.host + "'s password: ", false);
            if (!pass)
            {
                r.cancelled = true;
                return r;
            }
            const int rc = libssh2_userauth_password_ex(s, o.user.c_str(), ulen, pass->c_str(),
                                                        static_cast<unsigned int>(pass->size()), nullptr);
            Wipe(*pass);
            if (rc == 0)
            {
                r.ok = true;
                r.method = "password";
                return r;
            }
            if (attempt + 1 < o.maxPasswordTries && notify)
                notify("Permission denied, please try again.\r\n");
        }
    }
    return r;
}

} // namespace winssh::ssh
