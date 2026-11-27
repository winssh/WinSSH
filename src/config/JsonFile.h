// Small file helpers for WinSSH's JSON settings files.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace winssh::config
{

// Reads a whole file as UTF-8 text (BOM removed). std::nullopt when it does not exist / cannot be read.
std::optional<std::string> ReadTextFile(const std::filesystem::path &path);

// Writes to "<path>.tmp" and then replaces `path`, so a crash never leaves a half-written file.
// Creates the parent folder if needed.
bool WriteTextFileAtomic(const std::filesystem::path &path, const std::string &content, std::string *error);

} // namespace winssh::config
