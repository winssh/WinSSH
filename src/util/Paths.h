#pragma once

#include <filesystem>
#include <string>

namespace winssh::util
{

// %USERPROFILE% (falls back to the current directory if unset).
std::filesystem::path HomeDir();

// %USERPROFILE%\.ssh
std::filesystem::path SshDir();

// WinSSH settings folder: %WINSSH_CONFIG_DIR% if set (tests, portable use), else %APPDATA%\WinSSH.
std::filesystem::path ConfigDir();

// "~/x" or "~\x" -> %USERPROFILE%\x (UTF-8 in and out). Other paths are returned unchanged.
std::string ExpandHome(const std::string &path);

} // namespace winssh::util
