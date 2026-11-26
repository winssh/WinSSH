// Moves bytes between the console and an SSH channel until the channel ends.
#pragma once

#include "app/EscapeKeys.h"
#include "console/Console.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <optional>
#include <string>

typedef struct _LIBSSH2_CHANNEL LIBSSH2_CHANNEL;

namespace winssh::console
{
class InputReader;
class OutputWriter;
} // namespace winssh::console

namespace winssh::highlight
{
class Highlighter;
}

namespace winssh::ssh
{
class Session;
}

namespace winssh::app
{

class Bridge
{
public:
    // `highlighter` may be null (no highlighting). `info` is shown by the Ctrl+] i command.
    Bridge(boost::asio::io_context &io, ssh::Session &session, LIBSSH2_CHANNEL *channel,
           console::OutputWriter &output, highlight::Highlighter *highlighter, std::string info);

    // Runs the event loop. Returns true when the session ended normally (remote EOF, or the user
    // chose Ctrl+] x), false when the connection failed (see Error()). Switches the session to
    // non-blocking mode while running and back to blocking mode before returning.
    bool Run(console::InputReader &input);

    const std::string &Error() const { return error_; }
    bool UserExited() const { return userExit_; }

    // Time to wait for more output before showing a held-back partial line.
    static constexpr std::chrono::milliseconds kFlushDelay{10};

private:
    void Pump();
    void ArmWaits();
    void Finish(bool ok, std::string error);
    void OnKeys(const std::string &keys);
    void Execute(Command command);
    void WriteRemote(std::string_view bytes);
    void WriteLocal(const std::string &text);
    void FlushHighlighter();

    boost::asio::io_context &io_;
    ssh::Session &session_;
    LIBSSH2_CHANNEL *channel_;
    console::OutputWriter &output_;
    highlight::Highlighter *highlighter_;
    std::string info_;
    EscapeKeys escape_;
    boost::asio::steady_timer flushTimer_;
    std::optional<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_;

    std::string pendingInput_;
    std::optional<console::Size> pendingResize_;
    bool readWait_ = false;
    bool writeWait_ = false;
    bool done_ = false;
    bool ok_ = false;
    bool userExit_ = false;
    std::string error_;
};

} // namespace winssh::app
