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

// Length of the longest prefix of `bytes` that does not end inside a multi-byte UTF-8 sequence.
// Used to hold back an incomplete character at the end of a network read until the next read.
// Invalid bytes are not held back (they count as complete).
size_t CompleteUtf8Prefix(std::string_view bytes);

} // namespace winssh::util
