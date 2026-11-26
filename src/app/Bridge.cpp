#include "app/Bridge.h"

#include "console/InputReader.h"
#include "console/OutputWriter.h"
#include "highlight/Highlighter.h"
#include "ssh/Session.h"

#include <boost/asio/post.hpp>

#include <libssh2.h>

namespace winssh::app
{
namespace asio = boost::asio;
using asio::ip::tcp;

Bridge::Bridge(asio::io_context &io, ssh::Session &session, LIBSSH2_CHANNEL *channel,
               console::OutputWriter &output, highlight::Highlighter *highlighter, std::string info)
    : io_(io), session_(session), channel_(channel), output_(output), highlighter_(highlighter),
      info_(std::move(info)), flushTimer_(io)
{
}

bool Bridge::Run(console::InputReader &input)
{
    libssh2_session_set_blocking(session_.Raw(), 0);
    io_.restart();
    work_.emplace(asio::make_work_guard(io_));

    input.Start(
        [this](std::string keys) {
            asio::post(io_, [this, keys = std::move(keys)] {
                if (!done_)
                    OnKeys(keys);
            });
        },
        [this](console::Size size) {
            asio::post(io_, [this, size] {
                if (done_)
                    return;
                pendingResize_ = size;
                Pump();
            });
        });

    asio::post(io_, [this] { Pump(); });
    io_.run();
    input.Stop();
    io_.restart();
    FlushHighlighter();
    libssh2_session_set_blocking(session_.Raw(), 1);
    return ok_;
}

void Bridge::OnKeys(const std::string &keys)
{
    EscapeResult r = escape_.Feed(keys);
    pendingInput_ += r.toRemote;
    for (Command c : r.commands)
    {
        Execute(c);
        if (done_)
            return;
    }
    Pump();
}

void Bridge::Execute(Command command)
{
    switch (command)
    {
    case Command::Help:
        WriteLocal(EscapeKeys::HelpText());
        break;
    case Command::ToggleHighlight:
        if (!highlighter_)
        {
            WriteLocal("highlighting is not available (started with --no-highlight)\r\n");
            break;
        }
        highlighter_->SetEnabled(!highlighter_->Enabled());
        WriteLocal(std::string("highlighting ") + (highlighter_->Enabled() ? "on" : "off") + "\r\n");
        break;
    case Command::Info:
        WriteLocal(info_ + "Highlighting: " +
                   (highlighter_ && highlighter_->Enabled() ? "on" : "off") +
                   (highlighter_ && highlighter_->InAltScreen() ? " (paused: full-screen app)" : "") + "\r\n");
        break;
    case Command::Exit:
        userExit_ = true;
        Finish(true, {});
        break;
    case Command::Unknown:
        WriteLocal("unknown command. Press Ctrl+] h for help\r\n");
        break;
    }
}

void Bridge::WriteRemote(std::string_view bytes)
{
    if (!highlighter_)
    {
        output_.Write(bytes);
        return;
    }
    output_.Write(highlighter_->Process(bytes));
    if (highlighter_->HasPending())
    {
        // Show a held-back partial line (e.g. a prompt) if no more output comes soon.
        flushTimer_.expires_after(kFlushDelay); // cancels a previous wait
        flushTimer_.async_wait([this](const boost::system::error_code &ec) {
            if (!ec && !done_)
                FlushHighlighter();
        });
    }
}

void Bridge::FlushHighlighter()
{
    if (highlighter_ && highlighter_->HasPending())
        output_.Write(highlighter_->Flush());
}

void Bridge::WriteLocal(const std::string &text)
{
    FlushHighlighter();
    output_.Write("\r\n\x1b[0;36m[winssh] " + text + "\x1b[0m");
}

void Bridge::Pump()
{
    if (done_)
        return;

    // 1. Remote output (stdout and stderr; with a PTY the server usually merges them).
    char buf[16384];
    for (int stream : {0, SSH_EXTENDED_DATA_STDERR})
    {
        for (;;)
        {
            const ssize_t n = libssh2_channel_read_ex(channel_, stream, buf, sizeof(buf));
            if (n > 0)
            {
                WriteRemote(std::string_view(buf, static_cast<size_t>(n)));
                continue;
            }
            if (n == 0 || n == LIBSSH2_ERROR_EAGAIN)
                break;
            Finish(false, session_.LastError());
            return;
        }
    }

    // 2. Window size change.
    if (pendingResize_)
    {
        const int rc = libssh2_channel_request_pty_size(channel_, pendingResize_->cols, pendingResize_->rows);
        if (rc != LIBSSH2_ERROR_EAGAIN)
            pendingResize_.reset(); // sent, or refused by the server (not fatal)
    }

    // 3. Keyboard input.
    while (!pendingInput_.empty())
    {
        const ssize_t n = libssh2_channel_write(channel_, pendingInput_.data(), pendingInput_.size());
        if (n > 0)
        {
            pendingInput_.erase(0, static_cast<size_t>(n));
            continue;
        }
        if (n == LIBSSH2_ERROR_EAGAIN)
            break;
        Finish(false, session_.LastError());
        return;
    }

    // 4. Remote side finished (shell exited).
    if (libssh2_channel_eof(channel_))
    {
        Finish(true, {});
        return;
    }

    ArmWaits();
}

void Bridge::ArmWaits()
{
    tcp::socket &sock = session_.Socket();

    // Always listen for incoming data; the server can send at any time.
    if (!readWait_)
    {
        readWait_ = true;
        sock.async_wait(tcp::socket::wait_read, [this](const boost::system::error_code &ec) {
            readWait_ = false;
            if (done_)
                return;
            if (ec)
            {
                Finish(false, ec.message());
                return;
            }
            Pump();
        });
    }

    // Wait for writability only when libssh2 is blocked on sending.
    const int dirs = libssh2_session_block_directions(session_.Raw());
    if ((dirs & LIBSSH2_SESSION_BLOCK_OUTBOUND) && !writeWait_)
    {
        writeWait_ = true;
        sock.async_wait(tcp::socket::wait_write, [this](const boost::system::error_code &ec) {
            writeWait_ = false;
            if (done_)
                return;
            if (ec)
            {
                Finish(false, ec.message());
                return;
            }
            Pump();
        });
    }
}

void Bridge::Finish(bool ok, std::string error)
{
    if (done_)
        return;
    done_ = true;
    ok_ = ok;
    error_ = std::move(error);
    boost::system::error_code ignored;
    session_.Socket().cancel(ignored);
    flushTimer_.cancel();
    work_.reset();
    io_.stop();
}

} // namespace winssh::app
