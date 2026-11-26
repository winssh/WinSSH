// Saved passwords in Windows Credential Manager (generic entries, current user only).
// Secrets never go to JSON files, logs or the command line.
#pragma once

#include <optional>
#include <string>

namespace winssh::vault
{

// Entry name shown in Control Panel > Credential Manager: "WinSSH:<profile-id>".
constexpr const char *kDefaultPrefix = "WinSSH:";

class PasswordVault
{
public:
    // `prefix` is only changed by tests, so they never touch real WinSSH entries.
    explicit PasswordVault(std::string prefix = kDefaultPrefix) : prefix_(std::move(prefix)) {}

    bool Save(const std::string &profileId, const std::string &user, const std::string &password,
              std::string *error = nullptr) const;
    // Returns std::nullopt when there is no saved password.
    std::optional<std::string> Load(const std::string &profileId) const;
    bool Exists(const std::string &profileId) const;
    // Returns true when an entry was deleted (false when there was none).
    bool Remove(const std::string &profileId) const;

private:
    std::string prefix_;
};

// Overwrites the string's memory before clearing it.
void Wipe(std::string &secret);

} // namespace winssh::vault
