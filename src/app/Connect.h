// Interactive SSH session: connect, verify host key, authenticate, run the shell.
#pragma once

#include "app/ConnectPlan.h"
#include "cli/CommandLine.h"

#include <string>

namespace winssh::app
{

// Exit code like OpenSSH: the remote shell's exit status, or 255 on connection/auth errors.
constexpr int kExitConnectionError = 255;

// Resolves the destination (profile or host) from the command line, then connects.
int RunConnect(const cli::Options &options);

// Connects with a resolved plan. `oneTimePassword` (e.g. typed in the profile dialog) is tried
// once before asking; it is not saved here.
int RunConnectPlan(const ConnectPlan &plan, const std::string *oneTimePassword);

} // namespace winssh::app
