#include "ssh/KnownHosts.h"

#include "util/Paths.h"
#include "util/Utf8.h"

#include <libssh2.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace winssh::ssh
{
namespace fs = std::filesystem;

namespace
{

struct KnownHostsDeleter
{
    void operator()(LIBSSH2_KNOWNHOSTS *p) const { libssh2_knownhost_free(p); }
};
using KnownHostsPtr = std::unique_ptr<LIBSSH2_KNOWNHOSTS, KnownHostsDeleter>;

// Maps the session's host-key type to the known_hosts key-type bits, so only entries of the same
// type are compared (an ECDSA entry must not "mismatch" an ed25519 key).
int KeyTypeMask(int hostkeyType)
{
    switch (hostkeyType)
    {
    case LIBSSH2_HOSTKEY_TYPE_RSA: return LIBSSH2_KNOWNHOST_KEY_SSHRSA;
    case LIBSSH2_HOSTKEY_TYPE_DSS: return LIBSSH2_KNOWNHOST_KEY_SSHDSS;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return LIBSSH2_KNOWNHOST_KEY_ECDSA_256;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return LIBSSH2_KNOWNHOST_KEY_ECDSA_384;
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return LIBSSH2_KNOWNHOST_KEY_ECDSA_521;
    case LIBSSH2_HOSTKEY_TYPE_ED25519: return LIBSSH2_KNOWNHOST_KEY_ED25519;
    default: return LIBSSH2_KNOWNHOST_KEY_UNKNOWN;
    }
}

fs::path ToPath(const std::string &utf8)
{
    return fs::path(util::Utf8ToWide(utf8));
}

// Loads entries line by line; returns the number of lines that could not be parsed.
int LoadLines(LIBSSH2_KNOWNHOSTS *nh, const std::string &path)
{
    std::ifstream in(ToPath(path), std::ios::binary);
    int skipped = 0;
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;
        if (libssh2_knownhost_readline(nh, line.c_str(), line.size(), LIBSSH2_KNOWNHOST_FILE_OPENSSH) != 0)
            ++skipped;
    }
    return skipped;
}

} // namespace

std::string DefaultKnownHostsPath()
{
    return util::WideToUtf8((util::SshDir() / L"known_hosts").wstring());
}

std::string KnownHostsName(const std::string &host, int port)
{
    return port == 22 ? host : "[" + host + "]:" + std::to_string(port);
}

HostKeyCheck CheckKnownHost(LIBSSH2_SESSION *session, const std::string &host, int port, const std::string &path)
{
    HostKeyCheck result;
    size_t keyLen = 0;
    int keyType = 0;
    const char *key = libssh2_session_hostkey(session, &keyLen, &keyType);
    if (!key)
    {
        result.error = "server sent no host key";
        return result;
    }

    KnownHostsPtr nh(libssh2_knownhost_init(session));
    if (!nh)
    {
        result.error = "cannot initialise known_hosts";
        return result;
    }
    LoadLines(nh.get(), path);

    libssh2_knownhost *found = nullptr;
    const int typemask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | KeyTypeMask(keyType);
    const int rc = libssh2_knownhost_checkp(nh.get(), host.c_str(), port, key, keyLen, typemask, &found);
    switch (rc)
    {
    case LIBSSH2_KNOWNHOST_CHECK_MATCH: result.status = HostKeyStatus::Match; break;
    case LIBSSH2_KNOWNHOST_CHECK_NOTFOUND: result.status = HostKeyStatus::NotFound; break;
    case LIBSSH2_KNOWNHOST_CHECK_MISMATCH: result.status = HostKeyStatus::Mismatch; break;
    default:
        result.status = HostKeyStatus::Error;
        result.error = "known_hosts check failed";
        break;
    }
    return result;
}

bool AddKnownHost(LIBSSH2_SESSION *session, const std::string &host, int port, const std::string &path,
                  std::string *error)
{
    auto fail = [error](std::string msg) {
        if (error)
            *error = std::move(msg);
        return false;
    };

    size_t keyLen = 0;
    int keyType = 0;
    const char *key = libssh2_session_hostkey(session, &keyLen, &keyType);
    if (!key)
        return fail("server sent no host key");

    KnownHostsPtr nh(libssh2_knownhost_init(session));
    if (!nh)
        return fail("cannot initialise known_hosts");

    const std::string name = KnownHostsName(host, port);
    libssh2_knownhost *entry = nullptr;
    const int typemask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | KeyTypeMask(keyType);
    if (libssh2_knownhost_addc(nh.get(), name.c_str(), nullptr, key, keyLen, nullptr, 0, typemask, &entry) != 0)
        return fail("cannot build known_hosts entry");

    char line[8192];
    size_t lineLen = 0;
    if (libssh2_knownhost_writeline(nh.get(), entry, line, sizeof(line), &lineLen, LIBSSH2_KNOWNHOST_FILE_OPENSSH) != 0)
        return fail("cannot format known_hosts entry");

    const fs::path file = ToPath(path);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);

    // Start on a new line if the file does not end with one.
    bool needNewline = false;
    if (fs::exists(file, ec) && fs::file_size(file, ec) > 0)
    {
        std::ifstream in(file, std::ios::binary);
        in.seekg(-1, std::ios::end);
        char last = 0;
        in.get(last);
        needNewline = last != '\n';
    }

    std::ofstream out(file, std::ios::binary | std::ios::app);
    if (!out)
        return fail("cannot open " + path + " for writing");
    if (needNewline)
        out << '\n';
    out.write(line, static_cast<std::streamsize>(lineLen)); // writeline adds the trailing '\n'
    if (!out)
        return fail("cannot write " + path);
    return true;
}

} // namespace winssh::ssh
