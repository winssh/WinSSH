#include "cli/CommandLine.h"

#include <charconv>

namespace winssh::cli
{
namespace
{

std::optional<int> ParsePort(std::string_view text)
{
    int value = 0;
    const char *first = text.data();
    const char *last = text.data() + text.size();
    auto [ptr, ec] = std::from_chars(first, last, value);
    if (text.empty() || ec != std::errc() || ptr != last || value < 1 || value > 65535)
        return std::nullopt;
    return value;
}

// "a, b,,c" -> {"a", "b", "c"}
std::vector<std::string> SplitList(const std::string &text)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find(',', pos);
        if (end == std::string::npos)
            end = text.size();
        std::string item = text.substr(pos, end - pos);
        const auto first = item.find_first_not_of(" \t");
        const auto last = item.find_last_not_of(" \t");
        if (first != std::string::npos)
            out.push_back(item.substr(first, last - first + 1));
        pos = end + 1;
    }
    return out;
}

ParseResult Fail(std::string message)
{
    ParseResult r;
    r.error = std::move(message);
    return r;
}

} // namespace

std::optional<Target> ParseTarget(std::string_view text, std::string *error)
{
    auto fail = [error](const char *message) -> std::optional<Target> {
        if (error)
            *error = message;
        return std::nullopt;
    };

    Target t;
    std::string_view rest = text;

    // The last '@' separates user and host, so user names containing '@' still work.
    if (const auto at = rest.rfind('@'); at != std::string_view::npos)
    {
        t.user = std::string(rest.substr(0, at));
        rest.remove_prefix(at + 1);
        if (t.user.empty())
            return fail("empty user name before '@'");
    }

    std::string_view portText;
    bool hasPort = false;
    if (!rest.empty() && rest.front() == '[')
    {
        // [ipv6] or [ipv6]:port
        const auto close = rest.find(']');
        if (close == std::string_view::npos)
            return fail("missing ']' in IPv6 address");
        t.host = std::string(rest.substr(1, close - 1));
        std::string_view after = rest.substr(close + 1);
        if (!after.empty())
        {
            if (after.front() != ':')
                return fail("unexpected text after ']'");
            portText = after.substr(1);
            hasPort = true;
        }
    }
    else
    {
        const auto first = rest.find(':');
        const auto last = rest.rfind(':');
        if (first != std::string_view::npos && first == last)
        {
            t.host = std::string(rest.substr(0, first));
            portText = rest.substr(first + 1);
            hasPort = true;
        }
        else
        {
            // No colon, or several colons (bare IPv6 address without a port).
            t.host = std::string(rest);
        }
    }

    if (t.host.empty())
        return fail("empty host name");
    if (hasPort)
    {
        t.port = ParsePort(portText);
        if (!t.port)
            return fail("port must be a number from 1 to 65535");
    }
    return t;
}

ParseResult ParseCommandLine(const std::vector<std::string> &args)
{
    Options o;
    std::optional<std::string> destination;
    std::optional<Action> special; // actions that do not connect
    auto setSpecial = [&](Action a) -> bool {
        if (special)
            return false;
        special = a;
        return true;
    };
    const char *kOneAction =
        "use only one of --check-rules, --test-rules, --init-rules, --save, --delete, --list, --import-ssh-config, "
        "--wt-install, --wt-uninstall";

    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string &a = args[i];
        // Next argument as the option's value, or nullptr when missing.
        auto nextValue = [&]() -> const std::string * { return i + 1 < args.size() ? &args[++i] : nullptr; };

        if (a == "-h" || a == "--help")
        {
            o.action = Action::ShowHelp;
            return {true, o, {}};
        }
        if (a == "-V" || a == "--version")
        {
            o.action = Action::ShowVersion;
            return {true, o, {}};
        }
        if (a == "-p")
        {
            const std::string *v = nextValue();
            if (!v)
                return Fail("option -p needs a port number");
            o.portFlag = ParsePort(*v);
            if (!o.portFlag)
                return Fail("port must be a number from 1 to 65535");
            continue;
        }
        if (a == "-l")
        {
            const std::string *v = nextValue();
            if (!v || v->empty())
                return Fail("option -l needs a user name");
            o.userFlag = *v;
            continue;
        }
        if (a == "-i")
        {
            const std::string *v = nextValue();
            if (!v || v->empty())
                return Fail("option -i needs a key file path");
            o.identityFile = *v;
            continue;
        }
        if (a == "--no-highlight")
        {
            o.highlight = false;
            continue;
        }
        if (a == "--highlight")
        {
            const std::string *v = nextValue();
            if (!v)
                return Fail("option --highlight needs rule set names, e.g. default,journal");
            o.highlightSets = SplitList(*v);
            if (o.highlightSets.empty())
                return Fail("option --highlight needs rule set names, e.g. default,journal");
            o.highlightSetsGiven = true;
            continue;
        }
        if (a == "--keepalive")
        {
            const std::string *v = nextValue();
            int sec = -1;
            if (v)
            {
                auto [ptr, ec] = std::from_chars(v->data(), v->data() + v->size(), sec);
                if (ec != std::errc() || ptr != v->data() + v->size())
                    sec = -1;
            }
            if (sec < 0 || sec > 3600)
                return Fail("option --keepalive needs seconds from 0 (off) to 3600");
            o.keepAliveSec = sec;
            continue;
        }
        if (a == "--host-key-policy")
        {
            const std::string *v = nextValue();
            if (!v || (*v != "strict" && *v != "accept-new" && *v != "warn-on-change"))
                return Fail("option --host-key-policy needs strict, accept-new or warn-on-change");
            o.hostKeyPolicy = *v;
            continue;
        }
        if (a == "--legacy-algorithms")
        {
            o.legacyAlgorithms = true;
            continue;
        }
        if (a == "--reconnect" || a == "--no-reconnect")
        {
            o.reconnect = a == "--reconnect";
            continue;
        }
        if (a == "--check-rules" || a == "--init-rules" || a == "--list" || a == "--import-ssh-config" ||
            a == "--wt-install" || a == "--wt-uninstall")
        {
            const Action act = a == "--check-rules" ? Action::CheckRules
                               : a == "--init-rules" ? Action::InitRules
                               : a == "--list"       ? Action::ListProfiles
                               : a == "--wt-install" ? Action::WtInstall
                               : a == "--wt-uninstall" ? Action::WtUninstall
                                                     : Action::ImportSshConfig;
            if (!setSpecial(act))
                return Fail(kOneAction);
            continue;
        }
        if (a == "--test-rules")
        {
            if (!setSpecial(Action::TestRules))
                return Fail(kOneAction);
            const std::string *v = nextValue();
            if (!v)
                return Fail("option --test-rules needs a text, e.g. --test-rules \"link down at 10.0.0.1\"");
            o.testText = *v;
            continue;
        }
        if (a == "--save" || a == "--delete")
        {
            if (!setSpecial(a == "--save" ? Action::SaveProfile : Action::DeleteProfile))
                return Fail(kOneAction);
            const std::string *v = nextValue();
            if (!v || v->empty() || (*v)[0] == '-')
                return Fail("option " + a + " needs a profile id, e.g. " + a + " cb2-lab");
            o.profileId = *v;
            continue;
        }
        if (a == "--save-password")
        {
            o.savePassword = true;
            continue;
        }
        if (!a.empty() && a.front() == '-')
            return Fail("unknown option: " + a);

        if (destination)
            return Fail("only one destination is supported (remote commands are not supported yet)");
        destination = a;
    }

    if (o.savePassword && special != Action::SaveProfile)
        return Fail("--save-password is only used with --save");

    if (special && *special != Action::SaveProfile)
    {
        if (destination)
            return Fail("this option does not take a destination");
        o.action = *special;
        return {true, o, {}};
    }

    if (!destination)
    {
        if (special == Action::SaveProfile)
            return Fail("--save needs a destination, e.g. --save cb2-lab root@10.0.0.108");
        if (o.portFlag || o.userFlag || !o.identityFile.empty())
            return Fail("options -p, -l and -i need a destination");
        o.action = Action::Picker;
        return {true, o, {}};
    }

    std::string error;
    auto target = ParseTarget(*destination, &error);
    if (!target)
        return Fail("invalid destination '" + *destination + "': " + error);

    o.action = special ? *special : Action::Connect;
    o.destination = *destination;
    o.destinationMayBeProfile = destination->find_first_of("@:[") == std::string::npos;
    o.host = target->host;
    o.user = !target->user.empty() ? target->user : o.userFlag.value_or("");
    o.port = target->port ? *target->port : o.portFlag.value_or(kDefaultPort);
    // Values typed in the destination count as explicit too.
    if (!target->user.empty())
        o.userFlag = target->user;
    if (target->port)
        o.portFlag = target->port;
    return {true, o, {}};
}

std::string UsageText()
{
    return "Usage:\n"
           "  winssh                          Open the connection picker\n"
           "  winssh [options] [user@]host[:port]\n"
           "  winssh [options] <profile>      Connect with a saved profile\n"
           "\n"
           "Connection options:\n"
           "  -p <port>               Port (default 22)\n"
           "  -l <user>               User name\n"
           "  -i <keyfile>            Private key file\n"
           "  --highlight <sets>      Highlight rule sets, comma separated (default: default)\n"
           "  --no-highlight          Turn off keyword highlighting\n"
           "  --keepalive <sec>       Send a keepalive every <sec> seconds (0 = off)\n"
           "  --host-key-policy <p>   strict (default), accept-new, or warn-on-change\n"
           "  --legacy-algorithms     Allow ssh-rsa and SHA-1 key exchange (old devices)\n"
           "  --reconnect             Reconnect when the connection drops (--no-reconnect: off)\n"
           "  -V, --version           Show version\n"
           "  -h, --help              Show this help\n"
           "\n"
           "Profiles (%APPDATA%\\WinSSH\\profiles.json):\n"
           "  winssh --save <id> [options] [user@]host[:port] [--save-password]\n"
           "  winssh --list                   List profiles and recent connections\n"
           "  winssh --delete <id>            Delete a profile and its saved password\n"
           "  winssh --import-ssh-config      Add profiles from ~/.ssh/config Host entries\n"
           "\n"
           "Windows Terminal:\n"
           "  winssh --wt-install             Add WinSSH and your profiles to the Windows Terminal menu\n"
           "  winssh --wt-uninstall           Remove them again\n"
           "\n"
           "Highlight rules (%APPDATA%\\WinSSH\\highlight.json):\n"
           "  winssh --init-rules             Create a starter rules file\n"
           "  winssh --check-rules            Validate the rules file and list rule sets\n"
           "  winssh --test-rules \"<text>\"    Show <text> highlighted with the selected sets\n"
           "\n"
           "In a session, press Ctrl+] then h for local commands (highlighting, info, exit).\n";
}

} // namespace winssh::cli
