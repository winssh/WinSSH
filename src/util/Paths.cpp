#include "util/Paths.h"

#include <windows.h>

#include <string>

namespace winssh::util
{
namespace
{
std::wstring GetEnv(const wchar_t *name)
{
    const DWORD n = ::GetEnvironmentVariableW(name, nullptr, 0);
    if (n <= 1)
        return {};
    std::wstring value(n, L'\0');
    const DWORD got = ::GetEnvironmentVariableW(name, value.data(), n);
    value.resize(got);
    return value;
}
} // namespace

std::filesystem::path HomeDir()
{
    const std::wstring home = GetEnv(L"USERPROFILE");
    if (!home.empty())
        return std::filesystem::path(home);
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

std::filesystem::path SshDir()
{
    return HomeDir() / L".ssh";
}

std::filesystem::path ConfigDir()
{
    const std::wstring custom = GetEnv(L"WINSSH_CONFIG_DIR");
    if (!custom.empty())
        return std::filesystem::path(custom);
    const std::wstring appData = GetEnv(L"APPDATA");
    if (!appData.empty())
        return std::filesystem::path(appData) / L"WinSSH";
    return HomeDir() / L"AppData" / L"Roaming" / L"WinSSH";
}

} // namespace winssh::util
