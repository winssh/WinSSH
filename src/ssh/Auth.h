// User authentication, in OpenSSH's order: agent keys, key files, keyboard-interactive, password.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;

namespace winssh::ssh
{

// Asks the user for a secret or an answer. Returns std::nullopt when the user cancels.
using Prompter = std::function<std::optional<std::string>(const std::string &prompt, bool echo)>;

struct AuthOptions
{
    std::string user;
    std::string host;
    std::string identityFile;           // -i; empty = default key files
    std::vector<std::string> keyFiles;  // filled by DefaultKeyFiles() when identityFile is empty
    bool useAgent = true;
    int maxPasswordTries = 3;
};

struct AuthResult
{
    bool ok = false;
    bool cancelled = false;
    std::string method;       // method that worked, e.g. "publickey (agent)"
    std::string serverMethods; // what the server offered, for error messages
};

// %USERPROFILE%\.ssh\id_ed25519, id_ecdsa, id_rsa — only the ones that exist.
std::vector<std::string> DefaultKeyFiles();

// Shows a message to the user (e.g. "Permission denied, please try again.").
using Notifier = std::function<void(const std::string &message)>;

AuthResult Authenticate(LIBSSH2_SESSION *session, const AuthOptions &options, const Prompter &prompt,
                        const Notifier &notify);

} // namespace winssh::ssh
