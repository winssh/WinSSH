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
    std::optional<int> flagPort;
    std::string flagUser;

    for (size_t i = 0; i < args.size(); ++i)
    {
        const std::string &a = args[i];
        // Next argument as the option's value, or nullptr when missing.
        auto nextValue = [&]() -> const std::string * {
            return i + 1 < args.size() ? &args[++i] : nullptr;
        };

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
            flagPort = ParsePort(*v);
            if (!flagPort)
                return Fail("port must be a number from 1 to 65535");
            continue;
        }
        if (a == "-l")
        {
            const std::string *v = nextValue();
            if (!v || v->empty())
                return Fail("option -l needs a user name");
            flagUser = *v;
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
        if (!a.empty() && a.front() == '-')
            return Fail("unknown option: " + a);

        if (destination)
            return Fail("only one destination is supported (remote commands are not supported yet)");
        destination = a;
    }

    if (!destination)
    {
        if (flagPort || !flagUser.empty() || !o.identityFile.empty())
            return Fail("options -p, -l and -i need a destination");
        o.action = Action::Picker;
        return {true, o, {}};
    }

    std::string error;
    auto target = ParseTarget(*destination, &error);
    if (!target)
        return Fail("invalid destination '" + *destination + "': " + error);

    // Values in the destination win over flags, like OpenSSH's user@host over -l.
    o.action = Action::Connect;
    o.host = target->host;
    o.user = !target->user.empty() ? target->user : flagUser;
    o.port = target->port ? *target->port : flagPort.value_or(kDefaultPort);
    return {true, o, {}};
}

std::string UsageText()
{
    return "Usage:\n"
           "  winssh                          Open the connection picker\n"
           "  winssh [options] [user@]host[:port]\n"
           "\n"
           "Options:\n"
           "  -p <port>       Port (default 22)\n"
           "  -l <user>       User name\n"
           "  -i <keyfile>    Private key file\n"
           "  --no-highlight  Turn off keyword highlighting\n"
           "  -V, --version   Show version\n"
           "  -h, --help      Show this help\n"
           "\n"
           "In a session, press Ctrl+] then h for local commands (highlighting, info, exit).\n";
}

} // namespace winssh::cli
