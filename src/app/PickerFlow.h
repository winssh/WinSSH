// `winssh` with no destination: the connection picker and the profile dialog.
#pragma once

#include "cli/CommandLine.h"

namespace winssh::app
{

// Shows the picker until the user connects (returns the session's exit code) or quits (0).
// Flags such as --no-highlight apply to the connection made from the picker.
int RunPicker(const cli::Options &options);

} // namespace winssh::app
