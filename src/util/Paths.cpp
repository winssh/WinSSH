#include "util/Paths.h"

#include <windows.h>

#include <string>

namespace winssh::util
{

std::filesystem::path HomeDir()
{
    const DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
    if (n > 1)
    {
        std::wstring value(n, L'\0');
        const DWORD got = ::GetEnvironmentVariableW(L"USERPROFILE", value.data(), n);
        value.resize(got);
        if (!value.empty())
            return std::filesystem::path(value);
    }
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

std::filesystem::path SshDir()
{
    return HomeDir() / L".ssh";
}

} // namespace winssh::util
