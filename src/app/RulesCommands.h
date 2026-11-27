// winssh --init-rules / --check-rules / --test-rules
#pragma once

#include "cli/CommandLine.h"

namespace winssh::app
{

int RunInitRules();
int RunCheckRules(const cli::Options &options);
int RunTestRules(const cli::Options &options);

} // namespace winssh::app
