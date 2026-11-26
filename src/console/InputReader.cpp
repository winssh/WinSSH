#include "console/InputReader.h"

#include "util/Utf8.h"

#include <windows.h>

namespace winssh::console
{

void Utf16Joiner::Push(wchar_t ch, std::string &out)
{
    if (ch >= 0xD800 && ch <= 0xDBFF)
    {
        high_ = ch; // wait for the low surrogate
        return;
    }
    if (ch >= 0xDC00 && ch <= 0xDFFF)
    {
        if (high_)
        {
            const wchar_t pair[2] = {high_, ch};
            out += util::WideToUtf8(std::wstring_view(pair, 2));
        }
        high_ = 0; // a lone low surrogate is dropped
        return;
    }
    high_ = 0;
    out += util::WideToUtf8(std::wstring_view(&ch, 1));
}

InputReader::~InputReader()
{
    Stop();
}

void InputReader::Start(KeysHandler onKeys, ResizeHandler onResize)
{
    onKeys_ = std::move(onKeys);
    onResize_ = std::move(onResize);
    stopEvent_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this] { Run(); });
}

void InputReader::Stop()
{
    if (stopEvent_)
        ::SetEvent(stopEvent_);
    if (thread_.joinable())
        thread_.join();
    if (stopEvent_)
    {
        ::CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

void InputReader::Run()
{
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    HANDLE handles[2] = {stopEvent_, in};
    Utf16Joiner joiner;
    Size lastSize = GetSize();
    INPUT_RECORD records[128];

    for (;;)
    {
        const DWORD w = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
        if (w != WAIT_OBJECT_0 + 1)
            return; // stop requested or wait failed

        DWORD count = 0;
        if (!::ReadConsoleInputW(in, records, static_cast<DWORD>(std::size(records)), &count))
            return;

        std::string keys;
        bool resized = false;
        for (DWORD i = 0; i < count; ++i)
        {
            const INPUT_RECORD &r = records[i];
            if (r.EventType == KEY_EVENT)
            {
                const KEY_EVENT_RECORD &k = r.Event.KeyEvent;
                if (!k.bKeyDown || k.uChar.UnicodeChar == 0)
                    continue; // VT input mode delivers text and sequences as key-down chars
                for (WORD n = 0; n < (k.wRepeatCount ? k.wRepeatCount : 1); ++n)
                    joiner.Push(k.uChar.UnicodeChar, keys);
            }
            else if (r.EventType == WINDOW_BUFFER_SIZE_EVENT)
            {
                resized = true;
            }
        }

        if (!keys.empty() && onKeys_)
            onKeys_(std::move(keys));
        if (resized && onResize_)
        {
            // The event carries the buffer size; the PTY needs the visible window size.
            const Size s = GetSize();
            if (s.cols != lastSize.cols || s.rows != lastSize.rows)
            {
                lastSize = s;
                onResize_(s);
            }
        }
    }
}

} // namespace winssh::console
