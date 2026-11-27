#include "util/Utf8.h"

#include <windows.h>

#include <climits>

namespace winssh::util
{

std::string WideToUtf8(std::wstring_view wide)
{
    if (wide.empty() || wide.size() > INT_MAX)
        return {};
    const int inLen = static_cast<int>(wide.size());
    const int outLen = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), inLen, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(outLen), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), inLen, out.data(), outLen, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(std::string_view utf8)
{
    if (utf8.empty() || utf8.size() > INT_MAX)
        return {};
    const int inLen = static_cast<int>(utf8.size());
    const int outLen = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), inLen, nullptr, 0);
    std::wstring out(static_cast<size_t>(outLen), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), inLen, out.data(), outLen);
    return out;
}

size_t CompleteUtf8Prefix(std::string_view bytes)
{
    // Look back at most 3 bytes for the lead byte of the last sequence.
    const size_t n = bytes.size();
    for (size_t back = 1; back <= 4 && back <= n; ++back)
    {
        const unsigned char c = static_cast<unsigned char>(bytes[n - back]);
        if ((c & 0xC0) == 0x80)
            continue; // continuation byte, keep looking for the lead byte
        size_t need = 1;
        if ((c & 0xE0) == 0xC0)
            need = 2;
        else if ((c & 0xF0) == 0xE0)
            need = 3;
        else if ((c & 0xF8) == 0xF0)
            need = 4;
        return back < need ? n - back : n;
    }
    return n; // only continuation bytes (invalid) or empty: nothing to hold back
}

} // namespace winssh::util
