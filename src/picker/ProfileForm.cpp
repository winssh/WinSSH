#include "picker/ProfileForm.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace winssh::picker
{
namespace
{

std::string Trim(const std::string &s)
{
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}

std::optional<int> ToInt(const std::string &s)
{
    const std::string t = Trim(s);
    int v = 0;
    auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), v);
    if (t.empty() || ec != std::errc() || p != t.data() + t.size())
        return std::nullopt;
    return v;
}

std::vector<std::string> SplitList(const std::string &text)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find(',', pos);
        if (end == std::string::npos)
            end = text.size();
        const std::string item = Trim(text.substr(pos, end - pos));
        if (!item.empty())
            out.push_back(item);
        pos = end + 1;
    }
    return out;
}

std::string JoinList(const std::vector<std::string> &items)
{
    std::string out;
    for (const auto &i : items)
        out += (out.empty() ? "" : ",") + i;
    return out;
}

} // namespace

ProfileForm FormFromProfile(const config::Profile &p, bool hasSavedPassword)
{
    ProfileForm f;
    f.id = p.id;
    f.originalId = p.id;
    f.name = p.name == p.id ? "" : p.name;
    f.host = p.host;
    f.port = std::to_string(p.port);
    f.user = p.user;
    f.identityFile = p.identityFile;
    f.savePassword = p.savePassword;
    f.highlight = JoinList(p.highlight);
    f.keepAlive = std::to_string(p.keepAliveSec);
    f.hostKeyPolicy = p.hostKeyPolicy;
    f.legacyAlgorithms = p.legacyAlgorithms;
    f.autoReconnect = p.autoReconnect;
    f.hasSavedPassword = hasSavedPassword;
    return f;
}

ProfileForm FormFromRecent(const config::RecentEntry &r)
{
    ProfileForm f;
    f.host = r.host;
    f.port = std::to_string(r.port);
    f.user = r.user;
    return f;
}

std::optional<FormError> ValidateForm(const ProfileForm &f, const std::vector<config::Profile> &existing,
                                      bool forSave)
{
    if (forSave)
    {
        const std::string id = Trim(f.id);
        if (!config::IsValidProfileId(id))
            return FormError{kFieldId, "Profile ID: 1-64 letters, digits, '.', '_' or '-'."};
        // Conflict unless the profile found is the one being edited (ids are not case-sensitive).
        const config::Profile *other = config::FindProfile(existing, id);
        const config::Profile *self = f.originalId.empty() ? nullptr : config::FindProfile(existing, f.originalId);
        if (other && other != self)
            return FormError{kFieldId, "A profile with ID '" + other->id + "' already exists."};
    }
    const std::string host = Trim(f.host);
    if (host.empty())
        return FormError{kFieldHost, "Host is required."};
    if (host.find_first_of(" \t@") != std::string::npos)
        return FormError{kFieldHost, "Host must not contain spaces or '@' (put the user in the User field)."};
    const auto port = ToInt(f.port);
    if (!port || *port < 1 || *port > 65535)
        return FormError{kFieldPort, "Port must be a number from 1 to 65535."};
    const auto keepAlive = ToInt(f.keepAlive.empty() ? "0" : f.keepAlive);
    if (!keepAlive || *keepAlive < 0 || *keepAlive > 3600)
        return FormError{kFieldKeepAlive, "Keepalive must be 0 (off) to 3600 seconds."};
    for (const auto &set : SplitList(f.highlight))
        if (set.find_first_of(" \t") != std::string::npos)
            return FormError{kFieldHighlight, "Highlight sets are names separated by commas."};
    return std::nullopt;
}

config::Profile ProfileFromForm(const ProfileForm &f, const config::Profile *original)
{
    config::Profile p = original ? *original : config::Profile{};
    p.id = Trim(f.id);
    p.name = Trim(f.name).empty() ? p.id : Trim(f.name);
    p.host = Trim(f.host);
    p.port = ToInt(f.port).value_or(22);
    p.user = Trim(f.user);
    p.identityFile = Trim(f.identityFile);
    p.savePassword = f.savePassword;
    p.highlight = SplitList(f.highlight);
    p.keepAliveSec = ToInt(f.keepAlive.empty() ? "0" : f.keepAlive).value_or(0);
    p.hostKeyPolicy = f.hostKeyPolicy;
    p.legacyAlgorithms = f.legacyAlgorithms;
    p.autoReconnect = f.autoReconnect;
    return p;
}

std::string SuggestId(const std::string &host, const std::vector<config::Profile> &existing)
{
    std::string base;
    for (char c : Trim(host))
    {
        const unsigned char u = static_cast<unsigned char>(c);
        base += (std::isalnum(u) || c == '.' || c == '_' || c == '-') ? c : '-';
    }
    while (!base.empty() && base.front() == '-')
        base.erase(0, 1);
    if (base.size() > 56)
        base.resize(56);
    if (base.empty())
        base = "host";
    std::string id = base;
    for (int n = 2; config::FindProfile(existing, id); ++n)
        id = base + "-" + std::to_string(n);
    return id;
}

} // namespace winssh::picker
