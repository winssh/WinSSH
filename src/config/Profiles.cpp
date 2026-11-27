#include "config/Profiles.h"

#include "config/JsonFile.h"
#include "util/Paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <set>

namespace winssh::config
{
namespace
{
using nlohmann::json;

const std::set<std::string> kKnownFields = {"id",           "name",          "host",         "port",
                                            "user",         "identityFile",  "savePassword", "highlight",
                                            "keepAliveSec", "hostKeyPolicy", "legacyAlgorithms",
                                            "autoReconnect", "log", "screenTimestamps"};

bool EqualsNoCase(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

// Reads an optional field of the expected type; records an error and returns false on a type mismatch.
template <typename T, typename Check>
bool Field(const json &j, const char *key, T &out, Check isType, const char *typeText, const std::string &where,
           std::vector<std::string> &errors)
{
    if (!j.contains(key))
        return true;
    if (!isType(j[key]))
    {
        errors.push_back(where + ": \"" + key + "\" must be " + typeText);
        return false;
    }
    out = j[key].get<T>();
    return true;
}

} // namespace

std::string ToString(HostKeyPolicy policy)
{
    switch (policy)
    {
    case HostKeyPolicy::AcceptNew: return "accept-new";
    case HostKeyPolicy::WarnOnChange: return "warn-on-change";
    case HostKeyPolicy::Strict:
    default: return "strict";
    }
}

std::optional<HostKeyPolicy> ParseHostKeyPolicy(std::string_view text)
{
    if (text == "strict")
        return HostKeyPolicy::Strict;
    if (text == "accept-new")
        return HostKeyPolicy::AcceptNew;
    if (text == "warn-on-change")
        return HostKeyPolicy::WarnOnChange;
    return std::nullopt;
}

bool IsValidProfileId(std::string_view id)
{
    if (id.empty() || id.size() > 64 || id.front() == '-')
        return false;
    return std::all_of(id.begin(), id.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
    });
}

ProfileFile ParseProfiles(std::string_view text)
{
    ProfileFile pf;
    pf.fileFound = true;
    json root;
    try
    {
        root = json::parse(text.begin(), text.end(), nullptr, true, /*ignore_comments=*/true);
    }
    catch (const json::parse_error &e)
    {
        pf.errors.push_back(std::string("not valid JSON: ") + e.what());
        return pf;
    }
    if (!root.is_object() || !root.contains("profiles") || !root["profiles"].is_array())
    {
        pf.errors.push_back("expected { \"profiles\": [ ... ] }");
        return pf;
    }

    std::set<std::string> seen;
    size_t index = 0;
    for (const json &j : root["profiles"])
    {
        const std::string where = "profiles[" + std::to_string(index++) + "]";
        if (!j.is_object())
        {
            pf.errors.push_back(where + ": expected an object");
            continue;
        }
        Profile p;
        auto isStr = [](const json &v) { return v.is_string(); };
        auto isInt = [](const json &v) { return v.is_number_integer(); };
        auto isBool = [](const json &v) { return v.is_boolean(); };
        bool ok = Field(j, "id", p.id, isStr, "a string", where, pf.errors) &&
                  Field(j, "name", p.name, isStr, "a string", where, pf.errors) &&
                  Field(j, "host", p.host, isStr, "a string", where, pf.errors) &&
                  Field(j, "port", p.port, isInt, "a number", where, pf.errors) &&
                  Field(j, "user", p.user, isStr, "a string", where, pf.errors) &&
                  Field(j, "identityFile", p.identityFile, isStr, "a string", where, pf.errors) &&
                  Field(j, "savePassword", p.savePassword, isBool, "true or false", where, pf.errors) &&
                  Field(j, "keepAliveSec", p.keepAliveSec, isInt, "a number", where, pf.errors) &&
                  Field(j, "legacyAlgorithms", p.legacyAlgorithms, isBool, "true or false", where, pf.errors) &&
                  Field(j, "autoReconnect", p.autoReconnect, isBool, "true or false", where, pf.errors) &&
                  Field(j, "screenTimestamps", p.screenTimestamps, isBool, "true or false", where, pf.errors);
        if (!ok)
            continue;
        if (j.contains("highlight"))
        {
            const json &h = j["highlight"];
            if (!h.is_array() || !std::all_of(h.begin(), h.end(), [](const json &v) { return v.is_string(); }))
            {
                pf.errors.push_back(where + ": \"highlight\" must be an array of rule set names");
                continue;
            }
            for (const json &v : h)
                p.highlight.push_back(v.get<std::string>());
        }
        if (j.contains("log"))
        {
            const json &l = j["log"];
            const bool okShape = l.is_object() && (!l.contains("enabled") || l["enabled"].is_boolean()) &&
                                 (!l.contains("timestamps") || l["timestamps"].is_boolean());
            if (!okShape)
            {
                pf.errors.push_back(where + ": \"log\" must be { \"enabled\": true/false, \"timestamps\": true/false }");
                continue;
            }
            p.logEnabled = l.value("enabled", false);
            p.logTimestamps = l.value("timestamps", true);
        }
        if (j.contains("hostKeyPolicy"))
        {
            const auto policy = j["hostKeyPolicy"].is_string()
                                    ? ParseHostKeyPolicy(j["hostKeyPolicy"].get<std::string>())
                                    : std::nullopt;
            if (!policy)
            {
                pf.errors.push_back(where + ": \"hostKeyPolicy\" must be strict, accept-new or warn-on-change");
                continue;
            }
            p.hostKeyPolicy = *policy;
        }
        const std::string label = where + (p.id.empty() ? "" : " ('" + p.id + "')");
        if (!IsValidProfileId(p.id))
        {
            pf.errors.push_back(label + ": \"id\" must be 1-64 letters, digits, '.', '_' or '-'");
            continue;
        }
        if (p.host.empty())
        {
            pf.errors.push_back(label + ": \"host\" is required");
            continue;
        }
        if (p.port < 1 || p.port > 65535)
        {
            pf.errors.push_back(label + ": \"port\" must be 1-65535");
            continue;
        }
        if (p.keepAliveSec < 0)
        {
            pf.errors.push_back(label + ": \"keepAliveSec\" must be 0 (off) or more");
            continue;
        }
        std::string lowered = p.id;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!seen.insert(lowered).second)
        {
            pf.errors.push_back(label + ": duplicate id (ids are not case-sensitive)");
            continue;
        }
        if (p.name.empty())
            p.name = p.id;

        json extra = json::object();
        for (auto it = j.begin(); it != j.end(); ++it)
            if (kKnownFields.count(it.key()) == 0)
                extra[it.key()] = it.value();
        if (!extra.empty())
            p.extraJson = extra.dump();
        pf.profiles.push_back(std::move(p));
    }
    return pf;
}

ProfileFile LoadProfiles(const std::filesystem::path &path)
{
    const auto text = ReadTextFile(path);
    if (!text)
        return {};
    return ParseProfiles(*text);
}

std::string SerializeProfiles(const std::vector<Profile> &profiles)
{
    json arr = json::array();
    for (const Profile &p : profiles)
    {
        json j = json::object();
        j["id"] = p.id;
        if (!p.name.empty() && p.name != p.id)
            j["name"] = p.name;
        j["host"] = p.host;
        j["port"] = p.port;
        if (!p.user.empty())
            j["user"] = p.user;
        if (!p.identityFile.empty())
            j["identityFile"] = p.identityFile;
        if (p.savePassword)
            j["savePassword"] = true;
        if (!p.highlight.empty())
            j["highlight"] = p.highlight;
        if (p.keepAliveSec > 0)
            j["keepAliveSec"] = p.keepAliveSec;
        if (p.hostKeyPolicy != HostKeyPolicy::Strict)
            j["hostKeyPolicy"] = ToString(p.hostKeyPolicy);
        if (p.legacyAlgorithms)
            j["legacyAlgorithms"] = true;
        if (p.autoReconnect)
            j["autoReconnect"] = true;
        if (p.screenTimestamps)
            j["screenTimestamps"] = true;
        if (p.logEnabled || !p.logTimestamps)
            j["log"] = {{"enabled", p.logEnabled}, {"timestamps", p.logTimestamps}};
        if (!p.extraJson.empty())
        {
            const json extra = json::parse(p.extraJson, nullptr, false);
            if (extra.is_object())
                for (auto it = extra.begin(); it != extra.end(); ++it)
                    j[it.key()] = it.value();
        }
        arr.push_back(std::move(j));
    }
    json root = json::object();
    root["version"] = 1;
    root["profiles"] = std::move(arr);
    return root.dump(2) + "\n";
}

bool SaveProfiles(const std::filesystem::path &path, const std::vector<Profile> &profiles, std::string *error)
{
    return WriteTextFileAtomic(path, SerializeProfiles(profiles), error);
}

const Profile *FindProfile(const std::vector<Profile> &profiles, std::string_view id)
{
    for (const Profile &p : profiles)
        if (EqualsNoCase(p.id, id))
            return &p;
    return nullptr;
}

Profile *FindProfile(std::vector<Profile> &profiles, std::string_view id)
{
    for (Profile &p : profiles)
        if (EqualsNoCase(p.id, id))
            return &p;
    return nullptr;
}

std::filesystem::path ProfilesPath()
{
    return util::ConfigDir() / L"profiles.json";
}

// ---- history ----

std::vector<RecentEntry> ParseHistory(std::string_view text)
{
    std::vector<RecentEntry> out;
    const json root = json::parse(text.begin(), text.end(), nullptr, false, true);
    if (!root.is_object() || !root.contains("recent") || !root["recent"].is_array())
        return out; // a broken history file is not worth an error: start fresh
    for (const json &j : root["recent"])
    {
        if (!j.is_object() || !j.contains("host") || !j["host"].is_string())
            continue;
        RecentEntry e;
        e.host = j["host"].get<std::string>();
        e.user = j.value("user", std::string());
        e.port = j.value("port", 22);
        e.profileId = j.value("profileId", std::string());
        e.lastConnected = j.value("lastConnected", std::string());
        if (e.host.empty() || e.port < 1 || e.port > 65535)
            continue;
        out.push_back(std::move(e));
        if (out.size() == kMaxRecent)
            break;
    }
    return out;
}

std::vector<RecentEntry> LoadHistory(const std::filesystem::path &path)
{
    const auto text = ReadTextFile(path);
    return text ? ParseHistory(*text) : std::vector<RecentEntry>{};
}

std::string SerializeHistory(const std::vector<RecentEntry> &recent)
{
    json arr = json::array();
    for (const RecentEntry &e : recent)
    {
        json j = {{"host", e.host}, {"port", e.port}};
        if (!e.user.empty())
            j["user"] = e.user;
        if (!e.profileId.empty())
            j["profileId"] = e.profileId;
        if (!e.lastConnected.empty())
            j["lastConnected"] = e.lastConnected;
        arr.push_back(std::move(j));
    }
    return json{{"version", 1}, {"recent", std::move(arr)}}.dump(2) + "\n";
}

bool SaveHistory(const std::filesystem::path &path, const std::vector<RecentEntry> &recent, std::string *error)
{
    return WriteTextFileAtomic(path, SerializeHistory(recent), error);
}

void RecordRecent(std::vector<RecentEntry> &recent, RecentEntry entry)
{
    auto same = [&entry](const RecentEntry &e) {
        if (!entry.profileId.empty() || !e.profileId.empty())
            return EqualsNoCase(e.profileId, entry.profileId);
        return e.user == entry.user && EqualsNoCase(e.host, entry.host) && e.port == entry.port;
    };
    recent.erase(std::remove_if(recent.begin(), recent.end(), same), recent.end());
    recent.insert(recent.begin(), std::move(entry));
    if (recent.size() > kMaxRecent)
        recent.resize(kMaxRecent);
}

std::string NowUtcIso()
{
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_s(&utc, &now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buf;
}

std::filesystem::path HistoryPath()
{
    return util::ConfigDir() / L"history.json";
}

} // namespace winssh::config
