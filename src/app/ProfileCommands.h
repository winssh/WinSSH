// winssh --save / --list / --delete / --import-ssh-config
#pragma once

#include "cli/CommandLine.h"

namespace winssh::app
{

int RunSaveProfile(const cli::Options &options);
int RunDeleteProfile(const cli::Options &options);
int RunListProfiles();
int RunImportSshConfig();

} // namespace winssh::app
