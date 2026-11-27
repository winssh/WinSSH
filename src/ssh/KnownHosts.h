// Host-key verification against an OpenSSH known_hosts file.
#pragma once

#include <string>

typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;

namespace winssh::ssh
{

enum class HostKeyStatus
{
    Match,    // a known_hosts entry of the same key type matches
    NotFound, // no entry of this key type for this host
    Mismatch, // an entry of this key type exists with a different key: possible attack
    Error,
};

struct HostKeyCheck
{
    HostKeyStatus status = HostKeyStatus::Error;
    std::string error;
};

// %USERPROFILE%\.ssh\known_hosts (shared with OpenSSH).
std::string DefaultKnownHostsPath();

// "host" for port 22, "[host]:port" otherwise (OpenSSH known_hosts format).
std::string KnownHostsName(const std::string &host, int port);

// Lines libssh2 cannot parse (e.g. @cert-authority) are skipped instead of failing the whole file.
HostKeyCheck CheckKnownHost(LIBSSH2_SESSION *session, const std::string &host, int port, const std::string &path);

// Appends one line for the server's current key. Never rewrites existing lines.
bool AddKnownHost(LIBSSH2_SESSION *session, const std::string &host, int port, const std::string &path,
                  std::string *error);

} // namespace winssh::ssh
