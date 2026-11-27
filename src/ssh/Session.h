// One SSH connection: TCP socket + libssh2 session.
#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <chrono>
#include <stdexcept>
#include <string>

typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;

namespace winssh::ssh
{

class Error : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// Host-key algorithms in the order we offer them: ed25519 first (OpenSSH's choice, so existing
// known_hosts entries match), then ECDSA and RSA-SHA2. Legacy ssh-rsa / ssh-dss (SHA-1) only
// when `legacy` is set (old switches and routers).
std::string HostKeyPreference(LIBSSH2_SESSION *session, bool legacy = false);

class Session
{
public:
    explicit Session(boost::asio::io_context &io);
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    // TCP connect (all resolved addresses) with a timeout. Throws Error.
    void Connect(const std::string &host, int port, std::chrono::seconds timeout);
    // SSH handshake in blocking mode. `legacyAlgorithms` also offers SHA-1 host keys and key
    // exchange and CBC ciphers for old devices. Throws Error.
    void Handshake(bool legacyAlgorithms = false);

    LIBSSH2_SESSION *Raw() const { return session_; }
    boost::asio::ip::tcp::socket &Socket() { return socket_; }

    // "ssh-ed25519" etc. and "SHA256:..." for the server's host key (after Handshake).
    std::string HostKeyType() const;
    std::string HostKeyFingerprint() const;
    std::string RemoteBanner() const;

    // libssh2's last error message for this session.
    std::string LastError() const;

    // Tell the server we are leaving and close the socket.
    void Disconnect(const char *reason);

private:
    boost::asio::io_context &io_;
    boost::asio::ip::tcp::socket socket_;
    LIBSSH2_SESSION *session_ = nullptr;
};

} // namespace winssh::ssh
