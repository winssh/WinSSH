#include "app/PickerFlow.h"

#include "app/Connect.h"
#include "app/ConnectPlan.h"
#include "config/Profiles.h"
#include "console/Console.h"
#include "picker/PickerLoop.h"
#include "picker/ProfileDialog.h"
#include "picker/ProfileForm.h"
#include "vault/PasswordVault.h"
#include "wt/Fragment.h"

#include <windows.h>

namespace winssh::app
{
namespace
{

void Write(const char *bytes)
{
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), bytes, static_cast<DWORD>(strlen(bytes)), &written, nullptr);
}

// Plan for a profile, with the command-line flags (e.g. --no-highlight) on top.
ConnectPlan PlanForProfile(const cli::Options &flags, const std::vector<config::Profile> &profiles,
                           const std::string &id)
{
    cli::Options o = flags;
    o.destination = id;
    o.destinationMayBeProfile = true;
    return ResolveConnect(o, profiles);
}

// Plan for a host typed or remembered without a profile.
ConnectPlan PlanForHost(const cli::Options &flags, const std::string &user, const std::string &host, int port)
{
    cli::Options o = flags;
    o.destinationMayBeProfile = false;
    o.host = host;
    o.port = port;
    o.user = user;
    o.userFlag.reset();
    o.portFlag.reset();
    return ResolveConnect(o, {});
}

// Saves the dialog's profile and its password. Returns a status message.
std::string SaveFromForm(picker::ProfileForm &form, std::vector<config::Profile> &profiles, config::Profile &saved)
{
    const vault::PasswordVault vault;
    config::Profile *original = form.originalId.empty() ? nullptr : config::FindProfile(profiles, form.originalId);
    saved = picker::ProfileFromForm(form, original);
    if (original)
        *original = saved;
    else
        profiles.push_back(saved);

    std::string error;
    if (!config::SaveProfiles(config::ProfilesPath(), profiles, &error))
        return "Could not save: " + error;
    const std::string wtError = wt::SyncIfInstalled(profiles);

    std::string note;
    const bool renamed = !form.originalId.empty() && form.originalId != saved.id;
    if (!saved.savePassword)
    {
        vault.Remove(form.originalId.empty() ? saved.id : form.originalId);
        if (renamed)
            vault.Remove(saved.id);
    }
    else if (!form.password.empty())
    {
        if (vault.Save(saved.id, saved.user, form.password, &error))
            note = ", password saved";
        else
            note = ", password NOT saved (" + error + ")";
        if (renamed)
            vault.Remove(form.originalId);
    }
    else if (renamed)
    {
        // Keep the saved password under the new id.
        if (auto old = vault.Load(form.originalId))
        {
            vault.Save(saved.id, saved.user, *old);
            vault::Wipe(*old);
            vault.Remove(form.originalId);
        }
    }
    if (!wtError.empty())
        note += " (" + wtError + ")";
    return "Saved profile '" + saved.id + "'" + note + ".";
}

} // namespace

int RunPicker(const cli::Options &flags)
{
    if (!console::StdinIsConsole())
    {
        console::PrintError("winssh: stdin is not a console; give a destination or run WinSSH in a terminal.\r\n");
        return 2;
    }
    console::ConsoleModeGuard guard;
    guard.EnableVtOutput();
    console::SetTitle("WinSSH");

    picker::PickerModel model;
    picker::PickerView view;
    std::string status;

    for (;;)
    {
        auto pf = config::LoadProfiles(config::ProfilesPath());
        const auto recent = config::LoadHistory(config::HistoryPath());
        model.SetData(pf.profiles, recent);
        if (!pf.errors.empty())
            status = "profiles.json has errors (" + pf.errors.front() + "); run winssh --list for details.";
        model.SetStatus(status);
        status.clear();

        guard.EnableRawInput();
        Write(picker::PickerView::kEnter);
        const picker::Action a = picker::RunPickerLoop(model, view);
        // Copy what we need: `a.item` points into the model, which is rebuilt next round.
        const picker::Item item = a.item ? *a.item : picker::Item{};
        Write(picker::PickerView::kLeave);
        guard.RestoreInput();
        // Keys typed right after Enter belong to the session that starts now.
        if (a.kind == picker::ActionKind::Connect || a.kind == picker::ActionKind::QuickConnect)
            picker::PushBackKeys(a.rest);

        switch (a.kind)
        {
        case picker::ActionKind::Quit:
            return 0;

        case picker::ActionKind::Connect:
            if (!item.profileId.empty() && config::FindProfile(pf.profiles, item.profileId))
                return RunConnectPlan(PlanForProfile(flags, pf.profiles, item.profileId), nullptr);
            return RunConnectPlan(PlanForHost(flags, item.recent.user, item.recent.host, item.recent.port), nullptr);

        case picker::ActionKind::QuickConnect:
        {
            std::string error;
            const auto target = cli::ParseTarget(a.text, &error);
            if (!target)
            {
                status = "Invalid destination: " + error;
                break;
            }
            return RunConnectPlan(PlanForHost(flags, target->user, target->host, target->port.value_or(22)), nullptr);
        }

        case picker::ActionKind::DeleteProfile:
        {
            if (!pf.errors.empty())
            {
                status = "Fix profiles.json first; it was not changed.";
                break;
            }
            config::Profile *p = config::FindProfile(pf.profiles, item.profileId);
            if (!p)
                break;
            const std::string id = p->id;
            pf.profiles.erase(pf.profiles.begin() + (p - pf.profiles.data()));
            std::string error;
            if (!config::SaveProfiles(config::ProfilesPath(), pf.profiles, &error))
            {
                status = "Could not delete: " + error;
                break;
            }
            const bool hadPassword = vault::PasswordVault().Remove(id);
            status = "Deleted profile '" + id + "'" + (hadPassword ? " and its saved password." : ".");
            if (const std::string wtError = wt::SyncIfInstalled(pf.profiles); !wtError.empty())
                status += " (" + wtError + ")";
            break;
        }

        case picker::ActionKind::NewProfile:
        case picker::ActionKind::EditProfile:
        case picker::ActionKind::SaveRecent:
        {
            if (!pf.errors.empty())
            {
                status = "Fix profiles.json first; it was not changed.";
                break;
            }
            picker::ProfileForm form;
            if (a.kind == picker::ActionKind::EditProfile)
            {
                const config::Profile *p = config::FindProfile(pf.profiles, item.profileId);
                if (!p)
                    break;
                form = picker::FormFromProfile(*p, vault::PasswordVault().Exists(p->id));
            }
            else if (a.kind == picker::ActionKind::SaveRecent)
            {
                form = picker::FormFromRecent(item.recent);
                form.id = picker::SuggestId(item.recent.host, pf.profiles);
            }
            else if (!model.Filter().empty() && model.FilterIsQuickTarget())
            {
                // Ctrl+N after typing user@host: start from it.
                std::string error;
                if (const auto t = cli::ParseTarget(model.Filter(), &error))
                {
                    form.host = t->host;
                    form.user = t->user;
                    form.port = std::to_string(t->port.value_or(22));
                    form.id = picker::SuggestId(t->host, pf.profiles);
                }
            }

            const picker::DialogResult result = picker::ShowProfileDialog(form, pf.profiles);
            if (result == picker::DialogResult::Cancel)
            {
                vault::Wipe(form.password);
                break;
            }
            if (result == picker::DialogResult::Connect)
            {
                // Connect with the dialog's values without saving them.
                config::Profile temp = picker::ProfileFromForm(form, nullptr);
                if (temp.id.empty())
                    temp.id = picker::SuggestId(temp.host, {});
                ConnectPlan plan = PlanForProfile(flags, {temp}, temp.id);
                plan.profileId.clear(); // not a saved profile: no history link, no password saving
                plan.savePassword = false;
                std::string pw = form.password;
                vault::Wipe(form.password);
                const int code = RunConnectPlan(plan, pw.empty() ? nullptr : &pw);
                vault::Wipe(pw);
                return code;
            }
            config::Profile saved;
            status = SaveFromForm(form, pf.profiles, saved);
            vault::Wipe(form.password);
            if (result == picker::DialogResult::SaveAndConnect && status.rfind("Saved", 0) == 0)
                return RunConnectPlan(PlanForProfile(flags, pf.profiles, saved.id), nullptr);
            break;
        }

        case picker::ActionKind::None:
            break;
        }
    }
}

} // namespace winssh::app
