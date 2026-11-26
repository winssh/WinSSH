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
    Picker,          // no destination: show the profile/recent picker
    Connect,         // destination given (host or profile id)
    ShowHelp,
    ShowVersion,
    CheckRules,      // --check-rules
    TestRules,       // --test-rules <text>
    InitRules,       // --init-rules
    SaveProfile,     // --save <id> destination
    DeleteProfile,   // --delete <id>
    ListProfiles,    // --list
    ImportSshConfig, // --import-ssh-config
};

struct Options
{
    Action action = Action::Picker;

    // Destination as typed, and whether it may name a profile (no '@', ':' or '[').
    std::string destination;
    bool destinationMayBeProfile = false;

    // From the destination and flags (the destination wins, like OpenSSH's user@host over -l).
    std::string user; // empty: decide later (profile, or local user name)
    std::string host;
    int port = kDefaultPort;

    // Flags given explicitly; they override a profile's values.
    std::optional<std::string> userFlag;  // -l
    std::optional<int> portFlag;          // -p
    std::string identityFile;             // -i
    bool highlightSetsGiven = false;      // --highlight
    std::optional<int> keepAliveSec;      // --keepalive <sec>
    std::optional<std::string> hostKeyPolicy; // --host-key-policy strict|accept-new|warn-on-change
    bool legacyAlgorithms = false;        // --legacy-algorithms

    bool highlight = true;                             // --no-highlight turns it off
    std::vector<std::string> highlightSets{"default"}; // --highlight a,b
    std::string testText;                              // --test-rules

    std::string profileId;    // --save / --delete
    bool savePassword = false; // --save-password (with --save)
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
