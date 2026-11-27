#include "app/Reconnect.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <string>

namespace winssh::app
{
namespace
{
void Write(const std::string &text)
{
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}
} // namespace

int RetryDelaySeconds(int attempt)
{
    if (attempt <= 1)
        return 1;
    if (attempt >= 5)
        return 10;
    return std::min(10, 1 << (attempt - 1)); // 2, 4, 8
}

std::string TerminalResetSequence(bool leaveAltScreen)
{
    // Leaving the alternate screen also restores the cursor saved on entering it, so it is only
    // sent when the session was in it (otherwise the cursor jumps to the top of the screen).
    return std::string(leaveAltScreen ? "\x1b[?1049l" : "") +
           "\x1b[0m"        // colors and attributes
           "\x1b[?25h"      // show the cursor
           "\x1b[?1000l\x1b[?1002l\x1b[?1003l\x1b[?1006l" // mouse reporting off
           "\x1b[?2004l"    // bracketed paste off
           "\x1b[?1l\x1b>"; // normal cursor keys and keypad
}

WaitChoice WaitBeforeRetry(const std::string &target, int attempt, int seconds)
{
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(seconds);
    bool armedEscape = false; // Ctrl+] pressed: next key is a command (x = stop)
    int shown = -1;

    for (;;)
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
        if (left <= 0)
        {
            Write("\r\x1b[K");
            return WaitChoice::RetryNow;
        }
        const int secs = static_cast<int>((left + 999) / 1000);
        if (secs != shown)
        {
            shown = secs;
            Write("\r\x1b[K\x1b[36m[winssh] reconnecting to " + target + " in " + std::to_string(secs) + " s (attempt " +
                  std::to_string(attempt) + ") - Enter: now, Esc: stop\x1b[0m");
        }

        if (::WaitForSingleObject(in, static_cast<DWORD>(std::min<long long>(left, 200))) != WAIT_OBJECT_0)
            continue;
        INPUT_RECORD records[64];
        DWORD count = 0;
        if (!::ReadConsoleInputW(in, records, static_cast<DWORD>(std::size(records)), &count))
            continue;
        // Key-down characters of this batch (VT input mode: arrows etc. arrive as ESC [ ... sequences).
        std::wstring keys;
        for (DWORD i = 0; i < count; ++i)
        {
            const INPUT_RECORD &r = records[i];
            if (r.EventType == KEY_EVENT && r.Event.KeyEvent.bKeyDown && r.Event.KeyEvent.uChar.UnicodeChar)
                keys += r.Event.KeyEvent.uChar.UnicodeChar;
        }
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const wchar_t ch = keys[i];
            // An escape sequence (arrow, function key) is not the Esc key: skip it.
            if (ch == 0x1b && i + 1 < keys.size() && (keys[i + 1] == L'[' || keys[i + 1] == L'O'))
            {
                i += 2;
                while (i < keys.size() && !(keys[i] >= 0x40 && keys[i] <= 0x7e))
                    ++i;
                continue;
            }
            if (armedEscape)
            {
                armedEscape = false;
                if (ch == L'x' || ch == L'X' || ch == 0x18)
                {
                    Write("\r\x1b[K");
                    return WaitChoice::Stop;
                }
                if (ch == L'r' || ch == L'R' || ch == 0x12)
                {
                    Write("\r\x1b[K");
                    return WaitChoice::RetryNow;
                }
                continue;
            }
            if (ch == 0x1d) // Ctrl+]
                armedEscape = true;
            else if (ch == L'\r' || ch == L'r' || ch == L'R')
            {
                Write("\r\x1b[K");
                return WaitChoice::RetryNow;
            }
            else if (ch == 0x1b || ch == 0x03 || ch == L'q' || ch == L'Q') // Esc, Ctrl+C, q
            {
                Write("\r\x1b[K");
                return WaitChoice::Stop;
            }
        }
    }
}

} // namespace winssh::app
