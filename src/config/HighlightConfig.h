// Highlight rule sets from %APPDATA%\WinSSH\highlight.json (see docs/highlight-rules.md).
#pragma once

#include "highlight/Rules.h"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace winssh::config
{

constexpr const char *kDefaultRuleSet = "default";

struct RuleSets
{
    std::map<std::string, std::vector<highlight::Rule>> sets;
    std::vector<std::string> order; // set names in file order ("default" first if built in)
};

struct HighlightConfig
{
    RuleSets ruleSets;
    std::vector<std::string> errors;   // rules skipped, or the whole file when it is not valid JSON
    std::vector<std::string> warnings; // unknown fields and similar, nothing skipped
    bool fileFound = false;
};

// Parses the file content. Comments (// and /* */) are allowed. The built-in "default" set is
// present unless the file defines its own "default". On a JSON syntax error only built-ins remain.
HighlightConfig ParseHighlightConfig(std::string_view json);

// Reads and parses `path`. A missing file is not an error: built-in rules only, fileFound = false.
HighlightConfig LoadHighlightConfig(const std::filesystem::path &path);

// %APPDATA%\WinSSH\highlight.json (or under %WINSSH_CONFIG_DIR%).
std::filesystem::path HighlightConfigPath();

struct Selection
{
    std::vector<highlight::Rule> rules; // sets concatenated in the order asked (earlier wins)
    std::vector<std::string> warnings;  // unknown set names
};

Selection SelectRuleSets(const RuleSets &ruleSets, const std::vector<std::string> &names);

// Commented starter file written by `winssh --init-rules`.
std::string StarterConfig();

} // namespace winssh::config
