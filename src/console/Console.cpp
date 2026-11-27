#include "console/Console.h"

#include "util/Utf8.h"

#include <windows.h>

#include <mutex>

namespace winssh::console
{
namespace
{

void WriteText(HANDLE h, std::string_view utf8)
{
    if (h == nullptr || h == INVALID_HANDLE_VALUE || utf8.empty())
        return;
    DWORD mode = 0;
    if (::GetConsoleMode(h, &mode))
    {
        const std::wstring w = util::Utf8ToWide(utf8);
        DWORD written = 0;
        ::WriteConsoleW(h, w.data(), static_cast<DWORD>(w.size()), &written, nullptr);
    }
    else
    {
        DWORD written = 0;
        ::WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
}

std::mutex g_interruptMutex;
std::function<void()> g_interrupt;

BOOL WINAPI CtrlHandler(DWORD type)
{
    // Handle Ctrl+C / Ctrl+Break; let close/logoff/shutdown use the default handling.
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT)
        return FALSE;
    std::function<void()> handler;
    {
        std::lock_guard<std::mutex> lock(g_interruptMutex);
        handler = g_interrupt;
    }
    if (handler)
        handler();
    return TRUE;
}

} // namespace

ConsoleModeGuard::ConsoleModeGuard()
{
    in_ = ::GetStdHandle(STD_INPUT_HANDLE);
    out_ = ::GetStdHandle(STD_OUTPUT_HANDLE);
    haveIn_ = ::GetConsoleMode(in_, &inMode_) != FALSE;
    haveOut_ = ::GetConsoleMode(out_, &outMode_) != FALSE;
    inCp_ = ::GetConsoleCP();
    outCp_ = ::GetConsoleOutputCP();
}

ConsoleModeGuard::~ConsoleModeGuard()
{
    if (haveIn_)
        ::SetConsoleMode(in_, inMode_);
    if (haveOut_)
        ::SetConsoleMode(out_, outMode_);
    if (inCp_)
        ::SetConsoleCP(inCp_);
    if (outCp_)
        ::SetConsoleOutputCP(outCp_);
}

void ConsoleModeGuard::EnableVtOutput()
{
    ::SetConsoleCP(CP_UTF8);
    ::SetConsoleOutputCP(CP_UTF8);
    if (haveOut_)
    {
        ::SetConsoleMode(out_, outMode_ | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                                   DISABLE_NEWLINE_AUTO_RETURN);
    }
}

void ConsoleModeGuard::EnableRawInput()
{
    if (!haveIn_)
        return;
    DWORD mode = inMode_;
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT |
              ENABLE_QUICK_EDIT_MODE);
    mode |= ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
    ::SetConsoleMode(in_, mode);
}

void ConsoleModeGuard::RestoreInput()
{
    if (haveIn_)
        ::SetConsoleMode(in_, inMode_);
}

bool StdinIsConsole()
{
    DWORD mode = 0;
    return ::GetConsoleMode(::GetStdHandle(STD_INPUT_HANDLE), &mode) != FALSE;
}

Size GetSize()
{
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!::GetConsoleScreenBufferInfo(::GetStdHandle(STD_OUTPUT_HANDLE), &info))
        return {};
    Size s;
    s.cols = info.srWindow.Right - info.srWindow.Left + 1;
    s.rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    if (s.cols <= 0 || s.rows <= 0)
        return {};
    return s;
}

void SetTitle(std::string_view utf8)
{
    ::SetConsoleTitleW(util::Utf8ToWide(utf8).c_str());
}

void PrintError(std::string_view utf8)
{
    WriteText(::GetStdHandle(STD_ERROR_HANDLE), utf8);
}

std::optional<std::string> ReadLine(std::string_view prompt, bool echo)
{
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    DWORD saved = 0;
    if (!::GetConsoleMode(in, &saved))
        return std::nullopt;

    PrintError(prompt);
    DWORD mode = (saved | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_ECHO_INPUT) &
                 ~ENABLE_VIRTUAL_TERMINAL_INPUT;
    if (!echo)
        mode &= ~ENABLE_ECHO_INPUT;
    ::SetConsoleMode(in, mode);

    std::wstring line;
    bool complete = false;
    wchar_t buf[256];
    for (;;)
    {
        DWORD read = 0;
        if (!::ReadConsoleW(in, buf, static_cast<DWORD>(std::size(buf)), &read, nullptr) || read == 0)
            break; // Ctrl+C aborts the read
        line.append(buf, read);
        if (const auto nl = line.find(L'\n'); nl != std::wstring::npos)
        {
            line.erase(nl);
            complete = true;
            break;
        }
    }
    ::SetConsoleMode(in, saved);
    if (!echo)
        PrintError("\r\n");

    if (!line.empty() && line.back() == L'\r')
        line.pop_back();
    if (!complete || line.find(L'\x1a') != std::wstring::npos) // Ctrl+Z = EOF
        return std::nullopt;
    return util::WideToUtf8(line);
}

void SetInterruptHandler(std::function<void()> handler)
{
    static std::once_flag installed;
    {
        std::lock_guard<std::mutex> lock(g_interruptMutex);
        g_interrupt = std::move(handler);
    }
    std::call_once(installed, [] { ::SetConsoleCtrlHandler(CtrlHandler, TRUE); });
}

} // namespace winssh::console
