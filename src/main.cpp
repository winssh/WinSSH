// WinSSH entry point.
#include "Version.h"
#include "app/Connect.h"
#include "app/RulesCommands.h"
#include "cli/CommandLine.h"
#include "util/Utf8.h"

#include <windows.h>

#include <libssh2.h>

#include <cstdio>
#include <string>
#include <vector>

namespace
{

// Exit codes
constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitUsage = 2;

void PrintVersion()
{
    std::printf("%s %s\n", APP_NAME, APP_VERSION_FULL);
    std::printf("libssh2 %s\n", libssh2_version(0));
}

} // namespace

int wmain(int argc, wchar_t *argv[])
{
    // Print UTF-8 correctly in Windows Terminal and conhost.
    ::SetConsoleOutputCP(CP_UTF8);

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.push_back(winssh::util::WideToUtf8(argv[i]));

    const auto result = winssh::cli::ParseCommandLine(args);
    if (!result.ok)
    {
        std::fprintf(stderr, "winssh: %s\n\n%s", result.error.c_str(), winssh::cli::UsageText().c_str());
        return kExitUsage;
    }

    const auto &o = result.options;
    switch (o.action)
    {
    case winssh::cli::Action::ShowHelp:
        std::printf("%s %s - SSH terminal for Windows\n\n%s", APP_NAME, APP_VERSION_STR,
                    winssh::cli::UsageText().c_str());
        return kExitOk;

    case winssh::cli::Action::ShowVersion:
        PrintVersion();
        return kExitOk;

    case winssh::cli::Action::Picker:
        std::fprintf(stderr, "winssh: the connection picker is not implemented yet (M4).\n\n%s",
                     winssh::cli::UsageText().c_str());
        return kExitError;

    case winssh::cli::Action::Connect:
        return winssh::app::RunConnect(o);

    case winssh::cli::Action::CheckRules:
        return winssh::app::RunCheckRules(o);

    case winssh::cli::Action::TestRules:
        return winssh::app::RunTestRules(o);

    case winssh::cli::Action::InitRules:
        return winssh::app::RunInitRules();
    }
    return kExitError;
}
