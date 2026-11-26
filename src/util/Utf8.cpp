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

} // namespace winssh::util
