#include "app/Connect.h"

#include "Version.h"

#include "app/Bridge.h"
#include "app/ConnectPlan.h"
#include "config/Profiles.h"
#include "vault/PasswordVault.h"
#include "app/RulesSource.h"
#include "config/HighlightConfig.h"
#include "console/Console.h"
#include "console/InputReader.h"
#include "console/OutputWriter.h"
#include "highlight/Highlighter.h"
#include "highlight/Rules.h"
#include "ssh/Auth.h"
#include "ssh/KnownHosts.h"
#include "ssh/Session.h"
#include "util/Utf8.h"

#include <windows.h>

#include <libssh2.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>

namespace winssh::app
{
namespace
{

using console::PrintError;

std::string LocalUserName()
{
    wchar_t buf[257];
    DWORD len = static_cast<DWORD>(std::size(buf));
    if (::GetUserNameW(buf, &len) && len > 1)
        return util::WideToUtf8(std::wstring_view(buf, len - 1));
    return {};
}

// "ssh-ed25519" -> "ED25519", like OpenSSH's messages.
std::string KeyTypeLabel(const std::string &alg)
{
    if (alg == "ssh-ed25519")
        return "ED25519";
    if (alg.rfind("ecdsa-", 0) == 0)
        return "ECDSA";
    if (alg.rfind("rsa-", 0) == 0 || alg == "ssh-rsa")
        return "RSA";
    return alg;
}

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Asks until the user types yes or no. Cancel (Ctrl+C / Ctrl+Z) counts as no.
bool AskYesNo(const std::string &question)
{
    for (;;)
    {
        auto answer = console::ReadLine(question, true);
        if (!answer)
            return false;
        const std::string a = Lower(*answer);
        if (a == "yes")
            return true;
        if (a == "no")
            return false;
        PrintError("Please type 'yes' or 'no'.\r\n");
    }
}

void AddHostKey(ssh::Session &session, const std::string &host, int port, const std::string &path,
                const std::string &name, const std::string &type)
{
    std::string error;
    if (ssh::AddKnownHost(session.Raw(), host, port, path, &error))
        PrintError("Warning: Permanently added '" + name + "' (" + type + ") to the list of known hosts.\r\n");
    else
        PrintError("Warning: could not save the host key: " + error + "\r\n");
}

// Returns true when the connection may continue.
bool VerifyHostKey(ssh::Session &session, const std::string &host, int port, config::HostKeyPolicy policy)
{
    const std::string path = ssh::DefaultKnownHostsPath();
    const auto check = ssh::CheckKnownHost(session.Raw(), host, port, path);
    const std::string type = KeyTypeLabel(session.HostKeyType());
    const std::string fp = session.HostKeyFingerprint();
    const std::string name = ssh::KnownHostsName(host, port);

    switch (check.status)
    {
    case ssh::HostKeyStatus::Match:
        return true;

    case ssh::HostKeyStatus::Mismatch:
        if (policy == config::HostKeyPolicy::WarnOnChange)
        {
            // Lab devices get a new host key on every reflash: warn, then let the user decide.
            PrintError("WARNING: the " + type + " host key for " + name + " has changed.\r\n"
                       "New key fingerprint: " + fp + "\r\n"
                       "This is expected after reflashing a device; otherwise it may be an attack.\r\n");
            if (!AskYesNo("Trust the new key and continue (yes/no)? "))
            {
                PrintError("Host key verification failed.\r\n");
                return false;
            }
            AddHostKey(session, host, port, path, name, type);
            return true;
        }
        PrintError("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\r\n"
                   "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @\r\n"
                   "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\r\n"
                   "IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!\r\n"
                   "The " + type + " host key for " + name + " has changed.\r\n"
                   "The fingerprint for the key sent by the remote host is\r\n" + fp + ".\r\n"
                   "If the device was reflashed, remove the old entry from\r\n  " + path + "\r\n"
                   "(for example: ssh-keygen -R " + name + ") and connect again.\r\n"
                   "Host key verification failed.\r\n");
        return false;

    case ssh::HostKeyStatus::NotFound:
    {
        if (policy != config::HostKeyPolicy::AcceptNew)
        {
            PrintError("The authenticity of host '" + name + "' can't be established.\r\n" + type +
                       " key fingerprint is " + fp + ".\r\n");
            if (!AskYesNo("Are you sure you want to continue connecting (yes/no)? "))
            {
                PrintError("Host key verification failed.\r\n");
                return false;
            }
        }
        AddHostKey(session, host, port, path, name, type);
        return true;
    }

    case ssh::HostKeyStatus::Error:
    default:
        PrintError("winssh: host key check failed: " + check.error + "\r\n");
        return false;
    }
}

} // namespace

int RunConnect(const cli::Options &options)
{
    const auto profiles = config::LoadProfiles(config::ProfilesPath());
    for (const auto &e : profiles.errors)
        PrintError("winssh: profiles.json: " + e + "\r\n");
    return RunConnectPlan(ResolveConnect(options, profiles.profiles), nullptr);
}

int RunConnectPlan(const ConnectPlan &plan, const std::string *oneTimePassword)
{
    console::ConsoleModeGuard guard;
    guard.EnableVtOutput();

    if (!console::StdinIsConsole())
    {
        PrintError("winssh: stdin is not a console; run WinSSH in a terminal.\r\n");
        return kExitConnectionError;
    }

    const std::string user = plan.user.empty() ? LocalUserName() : plan.user;
    const std::string &host = plan.host;
    const int port = plan.port;
    const std::string target = user + "@" + host;

    boost::asio::io_context io;
    ssh::Session session(io);

    // Ctrl+C while connecting or at a prompt: stop cleanly (console modes restored by the guard).
    std::atomic<bool> interrupted{false};
    std::atomic<SOCKET> sock{INVALID_SOCKET};
    struct InterruptScope
    {
        ~InterruptScope() { console::SetInterruptHandler(nullptr); }
    } interruptScope;
    console::SetInterruptHandler([&] {
        interrupted = true;
        io.stop();
        if (const SOCKET s = sock.load(); s != INVALID_SOCKET)
            ::shutdown(s, SD_BOTH); // makes a blocking handshake/auth call fail
    });
    auto stoppedByUser = [&] {
        if (interrupted)
            PrintError("\r\nwinssh: interrupted\r\n");
        return interrupted.load();
    };

    try
    {
        session.Connect(host, port, std::chrono::seconds(15));
        sock = static_cast<SOCKET>(session.Socket().native_handle());
        session.Handshake(plan.legacyAlgorithms);
    }
    catch (const ssh::Error &e)
    {
        if (!stoppedByUser())
            PrintError(std::string("winssh: ") + e.what() + "\r\n");
        return kExitConnectionError;
    }

    if (!VerifyHostKey(session, host, port, plan.hostKeyPolicy))
    {
        stoppedByUser();
        return kExitConnectionError;
    }

    // Saved password (profile with savePassword) from Windows Credential Manager.
    const vault::PasswordVault vault;
    std::optional<std::string> stored;
    if (oneTimePassword && !oneTimePassword->empty())
        stored = *oneTimePassword; // typed in the profile dialog
    else if (plan.savePassword && !plan.profileId.empty())
        stored = vault.Load(plan.profileId);

    ssh::AuthOptions auth;
    auth.user = user;
    auth.host = host;
    auth.identityFile = plan.identityFile;
    if (auth.identityFile.empty())
        auth.keyFiles = ssh::DefaultKeyFiles();
    auth.storedPassword = stored ? &*stored : nullptr;
    auto result = ssh::Authenticate(
        session.Raw(), auth, [](const std::string &prompt, bool echo) { return console::ReadLine(prompt, echo); },
        [](const std::string &message) { PrintError(message); });
    if (stored)
        vault::Wipe(*stored);

    if (result.ok && plan.savePassword && !plan.profileId.empty() && !result.typedPassword.empty())
    {
        std::string error;
        if (vault.Save(plan.profileId, user, result.typedPassword, &error))
            PrintError("Password saved in Windows Credential Manager (" + std::string(vault::kDefaultPrefix) +
                       plan.profileId + ").\r\n");
        else
            PrintError("winssh: could not save the password: " + error + "\r\n");
    }
    vault::Wipe(result.typedPassword);

    if (!result.ok)
    {
        // A cancelled prompt (Ctrl+C or Ctrl+Z) can finish before the Ctrl+C handler thread runs.
        if (result.cancelled && !interrupted)
            PrintError("winssh: interrupted\r\n");
        else if (!stoppedByUser())
            PrintError("winssh: " + target + ": Permission denied (" + result.serverMethods + ").\r\n");
        return kExitConnectionError;
    }

    LIBSSH2_CHANNEL *channel = libssh2_channel_open_session(session.Raw());
    if (!channel)
    {
        PrintError("winssh: cannot open a session channel: " + session.LastError() + "\r\n");
        return kExitConnectionError;
    }
    const console::Size size = console::GetSize();
    if (libssh2_channel_request_pty_ex(channel, "xterm-256color", 14, nullptr, 0, size.cols, size.rows, 0, 0) != 0 ||
        libssh2_channel_shell(channel) != 0)
    {
        PrintError("winssh: cannot start a shell: " + session.LastError() + "\r\n");
        libssh2_channel_free(channel);
        return kExitConnectionError;
    }

    // Remember the connection for the picker and --list.
    {
        auto recent = config::LoadHistory(config::HistoryPath());
        config::RecordRecent(recent, {user, host, port, plan.profileId, config::NowUtcIso()});
        std::string error;
        if (!config::SaveHistory(config::HistoryPath(), recent, &error))
            PrintError("winssh: could not update history: " + error + "\r\n");
    }

    console::SetTitle("WinSSH: " + (plan.profileId.empty() ? target : plan.displayName));
    console::SetInterruptHandler(nullptr); // raw mode: Ctrl+C is a key for the server, not a signal
    guard.EnableRawInput();

    std::unique_ptr<highlight::Highlighter> highlighter;
    std::unique_ptr<RulesSource> rules;
    if (plan.highlight)
    {
        rules = std::make_unique<RulesSource>(config::HighlightConfigPath(), plan.highlightSets);
        auto loaded = rules->Load();
        for (const auto &m : loaded.messages)
            PrintError("winssh: highlight " + m + "\r\n");
        // Created even without rules, so rules added to the file later are picked up.
        highlighter = std::make_unique<highlight::Highlighter>(loaded.compiled.matcher, loaded.compiled.styles);
    }

    const char *cipher = libssh2_session_methods(session.Raw(), LIBSSH2_METHOD_CRYPT_CS);
    const std::string info = "Connected to " + target + " port " + std::to_string(port) +
                             (plan.profileId.empty() ? "" : " (profile " + plan.profileId + ")") + "\r\n" +
                             "Server: " + session.RemoteBanner() + "\r\n" +
                             "Host key: " + KeyTypeLabel(session.HostKeyType()) + " " + session.HostKeyFingerprint() +
                             "\r\n" + "Cipher: " + (cipher ? cipher : "?") + "\r\n" + "Auth: " + result.method +
                             "\r\n" + "WinSSH " + APP_VERSION_FULL + "\r\n";

    console::OutputWriter output;
    console::InputReader input;
    Bridge bridge(io, session, channel, output, highlighter.get(), rules.get(), info);
    bridge.SetKeepAlive(plan.keepAliveSec);
    const bool ended = bridge.Run(input);
    output.Flush();
    guard.RestoreInput();

    if (!ended)
    {
        PrintError("\r\nwinssh: connection to " + host + " lost: " + bridge.Error() + "\r\n");
        libssh2_channel_free(channel);
        return kExitConnectionError;
    }

    if (bridge.UserExited())
    {
        libssh2_channel_close(channel);
        libssh2_channel_free(channel);
        session.Disconnect("user exit");
        PrintError("\x1b[0m\r\nDisconnected from " + host + ".\r\n");
        return 0;
    }

    libssh2_channel_close(channel);
    libssh2_channel_wait_closed(channel);
    int status = libssh2_channel_get_exit_status(channel);
    char *signal = nullptr;
    libssh2_channel_get_exit_signal(channel, &signal, nullptr, nullptr, nullptr, nullptr, nullptr);
    if (signal)
    {
        status = kExitConnectionError;
        libssh2_free(session.Raw(), signal);
    }
    libssh2_channel_free(channel);
    session.Disconnect("bye");
    PrintError("Connection to " + host + " closed.\r\n");
    return status;
}

} // namespace winssh::app
