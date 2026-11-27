// Connection profiles in %APPDATA%\WinSSH\profiles.json.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winssh::config
{

enum class HostKeyPolicy
{
    Strict,       // unknown host: ask; changed key: refuse (default, like OpenSSH)
    AcceptNew,    // unknown host: add without asking; changed key: refuse
    WarnOnChange, // changed key: warn and ask to continue (lab devices reflashed often)
};

std::string ToString(HostKeyPolicy policy);
std::optional<HostKeyPolicy> ParseHostKeyPolicy(std::string_view text);

struct Profile
{
    std::string id;   // what you type: winssh <id>
    std::string name; // display name (defaults to id)
    std::string host;
    int port = 22;
    std::string user;
    std::string identityFile;
    bool savePassword = false;          // password kept in Windows Credential Manager
    std::vector<std::string> highlight; // rule sets; empty = "default"
    int keepAliveSec = 0;               // 0 = off
    HostKeyPolicy hostKeyPolicy = HostKeyPolicy::Strict;
    bool legacyAlgorithms = false;      // allow ssh-rsa / SHA-1 key exchange for old devices
    bool autoReconnect = false;         // reconnect when the connection drops (device reboot)
    bool logEnabled = false;            // "log": { "enabled": ... } session log to a file
    bool logTimestamps = true;          // "log": { "timestamps": ... } time at the start of each logged line
    bool screenTimestamps = false;      // time at the start of each output line on screen
    std::string extraJson;              // fields WinSSH does not know yet, kept when saving
};

// 1-64 characters: letters, digits, '.', '_', '-'; must not start with '-'.
bool IsValidProfileId(std::string_view id);

struct ProfileFile
{
    std::vector<Profile> profiles;
    std::vector<std::string> errors;   // invalid entries are skipped
    std::vector<std::string> warnings;
    bool fileFound = false;
};

ProfileFile ParseProfiles(std::string_view json);
ProfileFile LoadProfiles(const std::filesystem::path &path);
std::string SerializeProfiles(const std::vector<Profile> &profiles);
bool SaveProfiles(const std::filesystem::path &path, const std::vector<Profile> &profiles, std::string *error);

// Case-insensitive id lookup. Returns nullptr when not found.
const Profile *FindProfile(const std::vector<Profile> &profiles, std::string_view id);
Profile *FindProfile(std::vector<Profile> &profiles, std::string_view id);

std::filesystem::path ProfilesPath();

// ---- Recent connections (history.json) ----

struct RecentEntry
{
    std::string user;
    std::string host;
    int port = 22;
    std::string profileId;     // empty for quick connects
    std::string lastConnected; // UTC, ISO 8601 ("2026-11-26T10:30:00Z")
};

constexpr size_t kMaxRecent = 20;

std::vector<RecentEntry> ParseHistory(std::string_view json);
std::vector<RecentEntry> LoadHistory(const std::filesystem::path &path);
std::string SerializeHistory(const std::vector<RecentEntry> &recent);
bool SaveHistory(const std::filesystem::path &path, const std::vector<RecentEntry> &recent, std::string *error);

// Moves the entry to the front (same profile, or same user@host:port for quick connects) and
// keeps at most kMaxRecent entries.
void RecordRecent(std::vector<RecentEntry> &recent, RecentEntry entry);

std::string NowUtcIso();
std::filesystem::path HistoryPath();

} // namespace winssh::config
