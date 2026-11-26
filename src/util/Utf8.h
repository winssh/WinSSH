// UTF-8 <-> UTF-16 conversion. WinSSH keeps text as UTF-8 internally and converts only at
// Windows API boundaries (command line, console, Credential Manager, file paths).
#pragma once

#include <string>
#include <string_view>

namespace winssh::util
{

// Invalid input sequences are replaced with U+FFFD.
std::string WideToUtf8(std::wstring_view wide);
std::wstring Utf8ToWide(std::string_view utf8);

} // namespace winssh::util
