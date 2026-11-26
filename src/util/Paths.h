#pragma once

#include <filesystem>

namespace winssh::util
{

// %USERPROFILE% (falls back to the current directory if unset).
std::filesystem::path HomeDir();

// %USERPROFILE%\.ssh
std::filesystem::path SshDir();

// WinSSH settings folder: %WINSSH_CONFIG_DIR% if set (tests, portable use), else %APPDATA%\WinSSH.
std::filesystem::path ConfigDir();

} // namespace winssh::util
