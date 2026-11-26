// Command-line parsing. Flags follow OpenSSH where they overlap (-p port, -l user, -i identity)
// so `winssh` can be typed like `ssh`.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winssh::cli
{

constexpr int kDefaultPort = 22;

// "[user@]host[:port]". IPv6 hosts need brackets when a port is given: "[fe80::1]:2222".
struct Target
{
    std::string user;        // empty when not given
    std::string host;
    std::optional<int> port; // empty when not given
};

// Returns std::nullopt and sets *error on invalid input.
std::optional<Target> ParseTarget(std::string_view text, std::string *error);

enum class Action
{
    Picker,      // no destination: show the profile/recent picker (M4)
    Connect,     // destination given
    ShowHelp,
    ShowVersion,
};

struct Options
{
    Action action = Action::Picker;
    std::string user;         // empty: decide later (profile, prompt, or local user name)
    std::string host;
    int port = kDefaultPort;
    std::string identityFile; // -i
};

struct ParseResult
{
    bool ok = false;
    Options options;
    std::string error; // set when ok == false
};

// args: command-line arguments without the program name, UTF-8.
ParseResult ParseCommandLine(const std::vector<std::string> &args);

std::string UsageText();

} // namespace winssh::cli
