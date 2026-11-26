#include "app/ProfileCommands.h"

#include "app/ConnectPlan.h"
#include "config/JsonFile.h"
#include "config/Profiles.h"
#include "config/SshConfigImport.h"
#include "console/Console.h"
#include "util/Utf8.h"
#include "vault/PasswordVault.h"
#include "wt/Fragment.h"

#include <windows.h>

namespace winssh::app
{
namespace
{
using console::PrintError;

void Out(const std::string &utf8)
{
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

std::string PathText(const std::filesystem::path &p)
{
    return util::WideToUtf8(p.wstring());
}

std::string Address(const std::string &user, const std::string &host, int port)
{
    std::string a = user.empty() ? host : user + "@" + host;
    if (port != 22)
        a += ":" + std::to_string(port);
    return a;
}

std::string Pad(const std::string &s, size_t width)
{
    return s.size() >= width ? s + " " : s + std::string(width - s.size(), ' ');
}

// Loads profiles; prints problems. Returns false when the file exists but has errors that would be
// lost by rewriting it (we refuse to save over a file we could not fully read).
bool LoadForEdit(config::ProfileFile &pf)
{
    pf = config::LoadProfiles(config::ProfilesPath());
    for (const auto &e : pf.errors)
        PrintError("winssh: " + PathText(config::ProfilesPath()) + ": " + e + "\r\n");
    if (!pf.errors.empty())
    {
        PrintError("winssh: fix profiles.json first; it was not changed.\r\n");
        return false;
    }
    return true;
}

bool Save(const std::vector<config::Profile> &profiles)
{
    std::string error;
    if (!config::SaveProfiles(config::ProfilesPath(), profiles, &error))
    {
        PrintError("winssh: " + error + "\r\n");
        return false;
    }
    if (const std::string wtError = wt::SyncIfInstalled(profiles); !wtError.empty())
        PrintError("winssh: " + wtError + "\r\n");
    return true;
}

} // namespace

int RunSaveProfile(const cli::Options &options)
{
    if (!config::IsValidProfileId(options.profileId))
    {
        PrintError("winssh: '" + options.profileId +
                   "' is not a valid profile id (1-64 letters, digits, '.', '_' or '-').\r\n");
        return 2;
    }
    config::ProfileFile pf;
    if (!LoadForEdit(pf))
        return 1;

    config::Profile *existing = config::FindProfile(pf.profiles, options.profileId);
    const config::Profile p = ProfileFromOptions(options, existing);
    const bool updated = existing != nullptr;
    if (existing)
        *existing = p;
    else
        pf.profiles.push_back(p);
    if (!Save(pf.profiles))
        return 1;

    Out(std::string(updated ? "Updated" : "Saved") + " profile '" + p.id + "': " + Address(p.user, p.host, p.port) +
        "\r\nConnect with: winssh " + p.id + "\r\n");
    if (p.savePassword)
        Out("The password will be saved in Windows Credential Manager after the first successful login.\r\n");
    return 0;
}

int RunDeleteProfile(const cli::Options &options)
{
    config::ProfileFile pf;
    if (!LoadForEdit(pf))
        return 1;
    const config::Profile *p = config::FindProfile(pf.profiles, options.profileId);
    if (!p)
    {
        PrintError("winssh: no profile '" + options.profileId + "' (see winssh --list)\r\n");
        return 1;
    }
    const std::string id = p->id;
    pf.profiles.erase(pf.profiles.begin() + (p - pf.profiles.data()));
    if (!Save(pf.profiles))
        return 1;
    const bool hadPassword = vault::PasswordVault().Remove(id);
    Out("Deleted profile '" + id + "'" + (hadPassword ? " and its saved password" : "") + ".\r\n");
    return 0;
}

int RunListProfiles()
{
    const auto pf = config::LoadProfiles(config::ProfilesPath());
    for (const auto &e : pf.errors)
        PrintError("winssh: profiles.json: " + e + "\r\n");

    const vault::PasswordVault vault;
    Out("Profiles (" + PathText(config::ProfilesPath()) + "):\r\n");
    if (pf.profiles.empty())
        Out("  (none) - add one with: winssh --save <id> user@host\r\n");
    for (const auto &p : pf.profiles)
    {
        std::string notes;
        if (!p.highlight.empty())
        {
            notes += "  highlight: ";
            for (size_t i = 0; i < p.highlight.size(); ++i)
                notes += (i ? "," : "") + p.highlight[i];
        }
        if (p.savePassword)
            notes += vault.Exists(p.id) ? "  password saved" : "  password: saved at next login";
        if (p.hostKeyPolicy != config::HostKeyPolicy::Strict)
            notes += "  host key: " + config::ToString(p.hostKeyPolicy);
        if (p.legacyAlgorithms)
            notes += "  legacy algorithms";
        if (p.keepAliveSec > 0)
            notes += "  keepalive " + std::to_string(p.keepAliveSec) + "s";
        Out("  " + Pad(p.id, 18) + Pad(Address(p.user, p.host, p.port), 28) +
            (p.name != p.id ? "\"" + p.name + "\"" : "") + notes + "\r\n");
    }

    const auto recent = config::LoadHistory(config::HistoryPath());
    Out("\r\nRecent:\r\n");
    if (recent.empty())
        Out("  (none)\r\n");
    for (const auto &r : recent)
        Out("  " + Pad(r.profileId.empty() ? Address(r.user, r.host, r.port) : r.profileId, 28) + r.lastConnected +
            "\r\n");
    return pf.errors.empty() ? 0 : 1;
}

int RunWtInstall()
{
    const auto pf = config::LoadProfiles(config::ProfilesPath());
    for (const auto &e : pf.errors)
        PrintError("winssh: profiles.json: " + e + " (profile skipped)\r\n");
    std::string error;
    if (!wt::WriteFragment(pf.profiles, &error))
    {
        PrintError("winssh: " + error + "\r\n");
        return 1;
    }
    Out("Windows Terminal profiles written to " + PathText(wt::FragmentPath()) + "\r\n" + "  WinSSH" +
        (pf.profiles.empty() ? "" : " + " + std::to_string(pf.profiles.size()) + " profile(s)") +
        ", started with " + PathText(wt::CurrentExePath()) + "\r\n" +
        "Open a new Windows Terminal window to see them in the dropdown menu.\r\n"
        "They update automatically when you add, change or delete WinSSH profiles.\r\n");
    return 0;
}

int RunWtUninstall()
{
    const bool was = wt::IsInstalled();
    std::string error;
    if (!wt::RemoveFragment(&error))
    {
        PrintError("winssh: " + error + "\r\n");
        return 1;
    }
    Out(was ? "Removed the WinSSH profiles from Windows Terminal.\r\n" : "WinSSH was not added to Windows Terminal.\r\n");
    return 0;
}

int RunImportSshConfig()
{
    const auto path = config::DefaultSshConfigPath();
    const auto text = config::ReadTextFile(path);
    if (!text)
    {
        PrintError("winssh: cannot read " + PathText(path) + "\r\n");
        return 1;
    }
    const auto imported = config::ParseSshConfig(*text);
    for (const auto &w : imported.warnings)
        PrintError("winssh: " + PathText(path) + ": " + w + "\r\n");

    config::ProfileFile pf;
    if (!LoadForEdit(pf))
        return 1;
    int added = 0;
    for (const auto &p : imported.profiles)
    {
        if (config::FindProfile(pf.profiles, p.id))
        {
            Out("  skipped " + p.id + " (a profile with this id exists)\r\n");
            continue;
        }
        pf.profiles.push_back(p);
        Out("  added   " + Pad(p.id, 18) + Address(p.user, p.host, p.port) + "\r\n");
        ++added;
    }
    if (added > 0 && !Save(pf.profiles))
        return 1;
    Out("Imported " + std::to_string(added) + " profile(s) from " + PathText(path) + ".\r\n");
    return 0;
}

} // namespace winssh::app
