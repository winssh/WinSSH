// Windows Terminal integration through a JSON fragment: one "WinSSH" profile (opens the picker)
// plus one WT profile per WinSSH profile, so they show up in the WT dropdown.
// https://learn.microsoft.com/windows/terminal/json-fragment-extensions
#pragma once

#include "config/Profiles.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace winssh::wt
{

// Name-based UUID (version 5, SHA-1) in WinSSH's own namespace, as "{xxxxxxxx-xxxx-...}".
// Stable for the same name, so WT keeps a user's per-profile overrides across rewrites.
std::string GuidV5(std::string_view name);

// GUID of the WT profile for a WinSSH profile id (ids are not case-sensitive), or of the base
// "WinSSH" profile when `profileId` is empty.
std::string ProfileGuid(std::string_view profileId);

// %LOCALAPPDATA%\Microsoft\Windows Terminal\Fragments\WinSSH (or %WINSSH_WT_FRAGMENTS_DIR%).
std::filesystem::path FragmentDir();
std::filesystem::path FragmentPath(); // ...\winssh.json

// Fragment JSON for the given profiles, launching `exePath`.
std::string BuildFragment(const std::vector<config::Profile> &profiles, const std::filesystem::path &exePath);

// Full path of the running WinSSH.exe.
std::filesystem::path CurrentExePath();

// Writes the fragment for these profiles with the running exe's path.
bool WriteFragment(const std::vector<config::Profile> &profiles, std::string *error);
bool RemoveFragment(std::string *error);
bool IsInstalled();

// After profiles change: rewrites the fragment only if it is installed (--wt-install or the
// installer). Returns an error message, or empty.
std::string SyncIfInstalled(const std::vector<config::Profile> &profiles);

} // namespace winssh::wt
