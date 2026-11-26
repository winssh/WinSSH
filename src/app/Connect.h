// Interactive SSH session: connect, verify host key, authenticate, run the shell.
#pragma once

#include "cli/CommandLine.h"

namespace winssh::app
{

// Exit code like OpenSSH: the remote shell's exit status, or 255 on connection/auth errors.
constexpr int kExitConnectionError = 255;

int RunConnect(const cli::Options &options);

} // namespace winssh::app
