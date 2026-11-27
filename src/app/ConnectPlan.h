// What to connect to and how: a profile's values with command-line flags on top.
#pragma once

#include "cli/CommandLine.h"
#include "config/Profiles.h"

#include <string>
#include <vector>

namespace winssh::app
{

struct ConnectPlan
{
    std::string profileId; // empty for a quick connect
    std::string displayName;
    std::string host;
    int port = cli::kDefaultPort;
    std::string user; // empty: local user name
    std::string identityFile;
    bool highlight = true;
    std::vector<std::string> highlightSets{"default"};
    int keepAliveSec = 0;
    config::HostKeyPolicy hostKeyPolicy = config::HostKeyPolicy::Strict;
    bool legacyAlgorithms = false;
    bool savePassword = false;
    bool reconnect = false; // reconnect when the connection drops
    bool logEnabled = false;       // session log from the start
    bool logTimestamps = true;     // timestamps in the log file
    bool screenTimestamps = false; // timestamps on screen from the start
};

// Keepalive used for reconnecting sessions when none is set, so a dead connection is noticed.
constexpr int kReconnectKeepAliveSec = 15;

// A destination without '@', ':' or '[' that matches a profile id (case-insensitive) uses that
// profile; flags given on the command line override its values. Otherwise the destination is a host.
ConnectPlan ResolveConnect(const cli::Options &options, const std::vector<config::Profile> &profiles);

// Builds the profile `winssh --save <id> ...` stores. `existing` keeps fields the flags do not set.
config::Profile ProfileFromOptions(const cli::Options &options, const config::Profile *existing);

} // namespace winssh::app
