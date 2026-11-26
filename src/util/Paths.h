#pragma once

#include <filesystem>

namespace winssh::util
{

// %USERPROFILE% (falls back to the current directory if unset).
std::filesystem::path HomeDir();

// %USERPROFILE%\.ssh
std::filesystem::path SshDir();

} // namespace winssh::util
