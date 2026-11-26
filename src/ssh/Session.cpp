#include "ssh/Session.h"

#include "util/Base64.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/steady_timer.hpp>

#include <libssh2.h>

#include <cstring>
#include <mutex>
#include <vector>

namespace winssh::ssh
{
namespace asio = boost::asio;
using asio::ip::tcp;

namespace
{
void EnsureLibssh2Init()
{
    static std::once_flag once;
    std::call_once(once, [] {
        if (libssh2_init(0) != 0)
            throw Error("libssh2_init failed");
    });
}
} // namespace

std::string HostKeyPreference(LIBSSH2_SESSION *session, bool legacy)
{
    static const char *kModern[] = {
        "ssh-ed25519",        "ecdsa-sha2-nistp256", "ecdsa-sha2-nistp384",
        "ecdsa-sha2-nistp521", "rsa-sha2-512",        "rsa-sha2-256",
    };
    static const char *kLegacy[] = {"ssh-rsa", "ssh-dss"};
    std::vector<const char *> kPreferred(std::begin(kModern), std::end(kModern));
    if (legacy)
        kPreferred.insert(kPreferred.end(), std::begin(kLegacy), std::end(kLegacy));
    const char **algs = nullptr;
    const int n = libssh2_session_supported_algs(session, LIBSSH2_METHOD_HOSTKEY, &algs);
    std::string pref;
    for (const char *want : kPreferred)
    {
        for (int i = 0; i < n; ++i)
        {
            if (std::strcmp(algs[i], want) == 0)
            {
                if (!pref.empty())
                    pref += ',';
                pref += want;
                break;
            }
        }
    }
    if (n > 0)
        libssh2_free(session, algs);
    return pref;
}

Session::Session(asio::io_context &io) : io_(io), socket_(io)
{
    EnsureLibssh2Init();
    session_ = libssh2_session_init();
    if (!session_)
        throw Error("cannot create SSH session");
}

Session::~Session()
{
    if (session_)
        libssh2_session_free(session_);
    boost::system::error_code ignored;
    socket_.close(ignored);
}

void Session::Connect(const std::string &host, int port, std::chrono::seconds timeout)
{
    tcp::resolver resolver(io_);
    boost::system::error_code ec;
    const auto endpoints = resolver.resolve(host, std::to_string(port), ec);
    if (ec)
        throw Error("cannot resolve " + host + ": " + ec.message());

    boost::system::error_code result = asio::error::would_block;
    bool timedOut = false;
    asio::steady_timer timer(io_, timeout);
    asio::async_connect(socket_, endpoints, [&](const boost::system::error_code &e, const tcp::endpoint &) {
        result = e;
        timer.cancel();
    });
    timer.async_wait([&](const boost::system::error_code &e) {
        if (!e)
        {
            timedOut = true;
            boost::system::error_code ignored;
            socket_.close(ignored);
        }
    });
    io_.restart();
    io_.run();
    io_.restart();

    if (timedOut)
        throw Error("connection to " + host + " port " + std::to_string(port) + " timed out");
    if (result)
        throw Error("cannot connect to " + host + " port " + std::to_string(port) + ": " + result.message());
    socket_.set_option(tcp::no_delay(true), ec); // interactive keystrokes
}

static std::string AllSupported(LIBSSH2_SESSION *session, int method)
{
    const char **algs = nullptr;
    const int n = libssh2_session_supported_algs(session, method, &algs);
    std::string list;
    for (int i = 0; i < n; ++i)
        list += (list.empty() ? "" : ",") + std::string(algs[i]);
    if (n > 0)
        libssh2_free(session, algs);
    return list;
}

void Session::Handshake(bool legacyAlgorithms)
{
    libssh2_session_set_blocking(session_, 1);
    libssh2_session_set_timeout(session_, 30000);
    const std::string pref = HostKeyPreference(session_, legacyAlgorithms);
    if (!pref.empty())
        libssh2_session_method_pref(session_, LIBSSH2_METHOD_HOSTKEY, pref.c_str());
    if (legacyAlgorithms)
    {
        // Offer everything libssh2 supports (SHA-1 key exchange, CBC ciphers) after the modern ones.
        for (int method : {LIBSSH2_METHOD_KEX, LIBSSH2_METHOD_CRYPT_CS, LIBSSH2_METHOD_CRYPT_SC,
                           LIBSSH2_METHOD_MAC_CS, LIBSSH2_METHOD_MAC_SC})
        {
            const std::string all = AllSupported(session_, method);
            if (!all.empty())
                libssh2_session_method_pref(session_, method, all.c_str());
        }
    }
    if (libssh2_session_handshake(session_, static_cast<libssh2_socket_t>(socket_.native_handle())) != 0)
        throw Error("SSH handshake failed: " + LastError());
}

std::string Session::HostKeyType() const
{
    const char *m = libssh2_session_methods(session_, LIBSSH2_METHOD_HOSTKEY);
    return m ? m : "";
}

std::string Session::HostKeyFingerprint() const
{
    const auto *h = reinterpret_cast<const unsigned char *>(libssh2_hostkey_hash(session_, LIBSSH2_HOSTKEY_HASH_SHA256));
    return h ? "SHA256:" + util::Base64NoPad(h, 32) : "";
}

std::string Session::RemoteBanner() const
{
    const char *b = libssh2_session_banner_get(session_);
    return b ? b : "";
}

std::string Session::LastError() const
{
    char *msg = nullptr;
    int len = 0;
    libssh2_session_last_error(session_, &msg, &len, 0);
    return msg ? std::string(msg, static_cast<size_t>(len)) : "unknown error";
}

void Session::Disconnect(const char *reason)
{
    libssh2_session_set_blocking(session_, 1);
    libssh2_session_disconnect(session_, reason);
    boost::system::error_code ignored;
    socket_.close(ignored);
}

} // namespace winssh::ssh
