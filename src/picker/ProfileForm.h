// Data behind the profile dialog: text fields as typed, validation, conversion to a Profile.
#pragma once

#include "config/Profiles.h"

#include <optional>
#include <string>
#include <vector>

namespace winssh::picker
{

struct ProfileForm
{
    std::string id;
    std::string name;
    std::string host;
    std::string port = "22";
    std::string user;
    std::string identityFile;
    bool savePassword = false;
    std::string password; // typed in the dialog (optional); wipe after use
    std::string highlight; // comma-separated rule sets; empty = default
    std::string keepAlive = "0";
    config::HostKeyPolicy hostKeyPolicy = config::HostKeyPolicy::Strict;
    bool legacyAlgorithms = false;
    bool autoReconnect = false;

    bool hasSavedPassword = false; // a password is already in Credential Manager
    std::string originalId;        // id of the profile being edited; empty for a new one
};

ProfileForm FormFromProfile(const config::Profile &profile, bool hasSavedPassword);
ProfileForm FormFromRecent(const config::RecentEntry &recent);

struct FormError
{
    int field = 0; // which field to focus (see ProfileDialog control ids); 0 = none
    std::string message;
};

// Field ids for FormError (the dialog maps them to controls).
enum FormField
{
    kFieldId = 1,
    kFieldHost,
    kFieldPort,
    kFieldKeepAlive,
    kFieldHighlight,
};

// Checks the form. `forSave` also requires a valid, unused id. Returns the first problem, if any.
std::optional<FormError> ValidateForm(const ProfileForm &form, const std::vector<config::Profile> &existing,
                                      bool forSave);

// Builds the profile from a valid form. `original` keeps fields the dialog does not show.
config::Profile ProfileFromForm(const ProfileForm &form, const config::Profile *original);

// A profile id derived from a host name, e.g. "10.0.0.108" -> "10.0.0.108", "my host" -> "my-host".
std::string SuggestId(const std::string &host, const std::vector<config::Profile> &existing);

} // namespace winssh::picker
