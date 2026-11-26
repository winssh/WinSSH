// Moves bytes between the console and an SSH channel until the channel ends.
#pragma once

#include "console/Console.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <optional>
#include <string>

typedef struct _LIBSSH2_CHANNEL LIBSSH2_CHANNEL;

namespace winssh::console
{
class InputReader;
class OutputWriter;
} // namespace winssh::console

namespace winssh::ssh
{
class Session;
}

namespace winssh::app
{

class Bridge
{
public:
    Bridge(boost::asio::io_context &io, ssh::Session &session, LIBSSH2_CHANNEL *channel,
           console::OutputWriter &output);

    // Runs the event loop. Returns true when the remote side ended the channel (EOF),
    // false when the connection failed (see Error()). Switches the session to non-blocking
    // mode while running and back to blocking mode before returning.
    bool Run(console::InputReader &input);

    const std::string &Error() const { return error_; }

private:
    void Pump();
    void ArmWaits();
    void Finish(bool ok, std::string error);

    boost::asio::io_context &io_;
    ssh::Session &session_;
    LIBSSH2_CHANNEL *channel_;
    console::OutputWriter &output_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;

    std::string pendingInput_;
    std::optional<console::Size> pendingResize_;
    bool readWait_ = false;
    bool writeWait_ = false;
    bool done_ = false;
    bool ok_ = false;
    std::string error_;
};

} // namespace winssh::app
