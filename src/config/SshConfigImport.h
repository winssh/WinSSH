// Turns Host entries from an OpenSSH config file (~/.ssh/config) into WinSSH profiles.
#pragma once

#include "config/Profiles.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace winssh::config
{

struct SshConfigImport
{
    std::vector<Profile> profiles; // one per concrete Host alias (no wildcards)
    std::vector<std::string> warnings;
};

// Supports Host (several aliases, wildcards, negation), HostName (with %h), User, Port,
// IdentityFile; values from earlier matching blocks win, like OpenSSH. Match and Include
// are not supported (warning).
SshConfigImport ParseSshConfig(std::string_view text);

// %USERPROFILE%\.ssh\config
std::filesystem::path DefaultSshConfigPath();

// OpenSSH-style pattern match: '*' any run, '?' one character (case-insensitive).
bool SshPatternMatch(std::string_view pattern, std::string_view text);

} // namespace winssh::config
