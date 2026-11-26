// Win32 dialog to create or edit a profile (opened from the picker).
#pragma once

#include "picker/ProfileForm.h"

#include <vector>

namespace winssh::picker
{

enum class DialogResult
{
    Cancel,
    Save,           // save, back to the picker
    Connect,        // connect without saving
    SaveAndConnect, // Enter
};

// Remembers the window that has focus at startup: in Windows Terminal this is the terminal window
// (GetConsoleWindow() is a hidden pseudo window there). Call once at the start of main().
void CaptureTerminalWindow();

// Shows the dialog centered on the terminal window and brings it to the front; gives focus back
// to the terminal when it closes. `form` is updated with what the user typed. Validation happens
// in the dialog (it stays open until the form is valid or the user cancels).
DialogResult ShowProfileDialog(ProfileForm &form, const std::vector<config::Profile> &existing);

} // namespace winssh::picker
