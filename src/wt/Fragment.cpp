#include "wt/Fragment.h"

#include "config/JsonFile.h"
#include "util/Utf8.h"

#include <windows.h>

#include <bcrypt.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

#pragma comment(lib, "bcrypt.lib")

namespace winssh::wt
{
namespace
{
using nlohmann::json;

// WinSSH's UUID namespace (random, fixed forever: changing it would duplicate everyone's WT profiles).
constexpr std::array<unsigned char, 16> kNamespace = {0x8f, 0x1c, 0x2a, 0x7e, 0x5b, 0x3d, 0x4e, 0x9a,
                                                      0xa6, 0xc2, 0x3d, 0x7b, 0x9e, 0x1f, 0x0c, 0x54};

std::array<unsigned char, 20> Sha1(const std::string &data)
{
    std::array<unsigned char, 20> out{};
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCRYPT_SUCCESS(::BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0)))
    {
        ::BCryptHash(alg, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char *>(data.data())),
                     static_cast<ULONG>(data.size()), out.data(), static_cast<ULONG>(out.size()));
        ::BCryptCloseAlgorithmProvider(alg, 0);
    }
    return out;
}

std::wstring GetEnv(const wchar_t *name)
{
    const DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (n <= 1)
        return {};
    std::wstring v(n, L'\0');
    v.resize(::GetEnvironmentVariableW(name, v.data(), n));
    return v;
}

std::string Lower(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// "#2d7d46" from the profile's "wtTab": { "color": ... }, if any.
std::string TabColor(const config::Profile &p)
{
    if (p.extraJson.empty())
        return {};
    const json extra = json::parse(p.extraJson, nullptr, false);
    if (!extra.is_object() || !extra.contains("wtTab") || !extra["wtTab"].is_object())
        return {};
    const json &tab = extra["wtTab"];
    if (!tab.contains("color") || !tab["color"].is_string())
        return {};
    const std::string c = tab["color"].get<std::string>();
    const bool ok = c.size() == 7 && c[0] == '#' &&
                    std::all_of(c.begin() + 1, c.end(), [](unsigned char ch) { return std::isxdigit(ch); });
    return ok ? c : std::string();
}

} // namespace

std::string GuidV5(std::string_view name)
{
    std::string data(reinterpret_cast<const char *>(kNamespace.data()), kNamespace.size());
    data.append(name);
    auto h = Sha1(data);
    h[6] = static_cast<unsigned char>((h[6] & 0x0F) | 0x50); // version 5
    h[8] = static_cast<unsigned char>((h[8] & 0x3F) | 0x80); // RFC 4122 variant
    char buf[40];
    std::snprintf(buf, sizeof(buf), "{%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x}", h[0], h[1],
                  h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[11], h[12], h[13], h[14], h[15]);
    return buf;
}

std::string ProfileGuid(std::string_view profileId)
{
    return GuidV5(profileId.empty() ? std::string("winssh:picker") : "winssh:profile:" + Lower(profileId));
}

std::filesystem::path FragmentDir()
{
    const std::wstring custom = GetEnv(L"WINSSH_WT_FRAGMENTS_DIR");
    if (!custom.empty())
        return std::filesystem::path(custom);
    return std::filesystem::path(GetEnv(L"LOCALAPPDATA")) / L"Microsoft" / L"Windows Terminal" / L"Fragments" /
           L"WinSSH";
}

std::filesystem::path FragmentPath()
{
    return FragmentDir() / L"winssh.json";
}

std::string BuildFragment(const std::vector<config::Profile> &profiles, const std::filesystem::path &exePath)
{
    const std::string exe = util::WideToUtf8(exePath.wstring());
    const std::string quoted = "\"" + exe + "\"";

    json list = json::array();
    list.push_back({{"guid", ProfileGuid("")},
                    {"name", "WinSSH"},
                    {"commandline", quoted},
                    {"icon", exe},
                    {"hidden", false}});
    for (const auto &p : profiles)
    {
        json wtp = {{"guid", ProfileGuid(p.id)},
                    {"name", "WinSSH: " + (p.name.empty() ? p.id : p.name)},
                    {"commandline", quoted + " " + p.id}, // ids contain no spaces or quotes
                    {"icon", exe},
                    {"hidden", false}};
        if (const std::string color = TabColor(p); !color.empty())
            wtp["tabColor"] = color;
        list.push_back(std::move(wtp));
    }
    return json{{"profiles", std::move(list)}}.dump(2) + "\n";
}

std::filesystem::path CurrentExePath()
{
    std::wstring buf(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n < buf.size())
        {
            buf.resize(n);
            return std::filesystem::path(buf);
        }
        buf.resize(buf.size() * 2);
    }
}

bool WriteFragment(const std::vector<config::Profile> &profiles, std::string *error)
{
    return config::WriteTextFileAtomic(FragmentPath(), BuildFragment(profiles, CurrentExePath()), error);
}

bool RemoveFragment(std::string *error)
{
    std::error_code ec;
    std::filesystem::remove(FragmentPath(), ec);
    if (ec)
    {
        if (error)
            *error = ec.message();
        return false;
    }
    std::filesystem::remove(FragmentDir(), ec); // only if empty; ignore errors
    return true;
}

bool IsInstalled()
{
    std::error_code ec;
    return std::filesystem::is_regular_file(FragmentPath(), ec);
}

std::string SyncIfInstalled(const std::vector<config::Profile> &profiles)
{
    if (!IsInstalled())
        return {};
    std::string error;
    if (!WriteFragment(profiles, &error))
        return "could not update the Windows Terminal profiles: " + error;
    return {};
}

} // namespace winssh::wt
