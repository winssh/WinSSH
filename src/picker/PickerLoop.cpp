#include "picker/PickerLoop.h"

#include "console/Console.h"
#include "console/InputReader.h"
#include "util/Utf8.h"

#include <windows.h>

#include <vector>

namespace winssh::picker
{
namespace
{
void Write(const std::string &bytes)
{
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
}
} // namespace

void PushBackKeys(const std::string &utf8)
{
    if (utf8.empty())
        return;
    const std::wstring w = util::Utf8ToWide(utf8);
    std::vector<INPUT_RECORD> recs;
    recs.reserve(w.size() * 2);
    for (wchar_t ch : w)
    {
        INPUT_RECORD r{};
        r.EventType = KEY_EVENT;
        r.Event.KeyEvent.bKeyDown = TRUE;
        r.Event.KeyEvent.wRepeatCount = 1;
        r.Event.KeyEvent.uChar.UnicodeChar = ch;
        recs.push_back(r);
        r.Event.KeyEvent.bKeyDown = FALSE;
        recs.push_back(r);
    }
    DWORD written = 0;
    ::WriteConsoleInputW(::GetStdHandle(STD_INPUT_HANDLE), recs.data(), static_cast<DWORD>(recs.size()), &written);
}

Action RunPickerLoop(PickerModel &model, PickerView &view)
{
    HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
    console::Utf16Joiner joiner;
    INPUT_RECORD records[128];

    for (;;)
    {
        const console::Size size = console::GetSize();
        Write(view.Render(model, size.cols, size.rows));

        if (::WaitForSingleObject(in, INFINITE) != WAIT_OBJECT_0)
            return {ActionKind::Quit, nullptr, {}};
        DWORD count = 0;
        if (!::ReadConsoleInputW(in, records, static_cast<DWORD>(std::size(records)), &count))
            return {ActionKind::Quit, nullptr, {}};

        std::string keys;
        for (DWORD i = 0; i < count; ++i)
        {
            const INPUT_RECORD &r = records[i];
            if (r.EventType != KEY_EVENT)
                continue; // resize and focus events just cause a redraw
            const KEY_EVENT_RECORD &k = r.Event.KeyEvent;
            if (!k.bKeyDown || k.uChar.UnicodeChar == 0)
                continue;
            for (WORD n = 0; n < (k.wRepeatCount ? k.wRepeatCount : 1); ++n)
                joiner.Push(k.uChar.UnicodeChar, keys);
        }
        if (keys.empty())
            continue;
        Action a = model.Feed(keys);
        if (a.kind != ActionKind::None)
            return a;
    }
}

} // namespace winssh::picker
