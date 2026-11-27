#include "app/ConnectPlan.h"
#include "app/EscapeKeys.h"
#include "app/Reconnect.h"
#include "cli/CommandLine.h"
#include "config/Profiles.h"
#include "picker/ProfileForm.h"

#include <catch2/catch_test_macros.hpp>

using namespace winssh;

namespace
{
cli::Options Parse(std::vector<std::string> args)
{
    auto r = cli::ParseCommandLine(args);
    REQUIRE(r.ok);
    return r.options;
}
} // namespace

TEST_CASE("Retry delays back off to 10 seconds", "[reconnect]")
{
    CHECK(app::RetryDelaySeconds(1) == 1);
    CHECK(app::RetryDelaySeconds(2) == 2);
    CHECK(app::RetryDelaySeconds(3) == 4);
    CHECK(app::RetryDelaySeconds(4) == 8);
    CHECK(app::RetryDelaySeconds(5) == 10);
    CHECK(app::RetryDelaySeconds(50) == 10);
    CHECK(app::RetryDelaySeconds(0) == 1);
}

TEST_CASE("Terminal reset leaves the alternate screen and shows the cursor", "[reconnect]")
{
    const std::string inAlt = app::TerminalResetSequence(true);
    CHECK(inAlt.rfind("\x1b[?1049l", 0) == 0); // first, before the other resets
    CHECK(inAlt.find("\x1b[?25h") != std::string::npos);
    CHECK(inAlt.find("\x1b[0m") != std::string::npos);

    // Not in the alternate screen: no ?1049l, which would restore a stale cursor position
    // (the top of the screen) and let the next session overwrite older lines.
    const std::string normal = app::TerminalResetSequence(false);
    CHECK(normal.find("1049") == std::string::npos);
    CHECK(normal.find("\x1b[?25h") != std::string::npos);
}

TEST_CASE("Ctrl+] r asks for a reconnect", "[reconnect]")
{
    app::EscapeKeys e;
    auto r = e.Feed("\x1dr");
    REQUIRE(r.commands.size() == 1);
    CHECK(r.commands[0] == app::Command::Reconnect);
    CHECK(app::EscapeKeys::HelpText().find("r  reconnect now") != std::string::npos);
}

TEST_CASE("Profiles: autoReconnect is a real field now", "[reconnect]")
{
    auto pf = config::ParseProfiles(R"({"profiles": [{"id": "cb2", "host": "h", "autoReconnect": true}]})");
    REQUIRE(pf.errors.empty());
    REQUIRE(pf.profiles.size() == 1);
    CHECK(pf.profiles[0].autoReconnect);
    CHECK(pf.profiles[0].extraJson.find("autoReconnect") == std::string::npos); // not an unknown field any more
    const std::string text = config::SerializeProfiles(pf.profiles);
    CHECK(text.find("\"autoReconnect\": true") != std::string::npos);

    auto bad = config::ParseProfiles(R"({"profiles": [{"id": "x", "host": "h", "autoReconnect": "yes"}]})");
    CHECK(bad.profiles.empty());
    CHECK_FALSE(bad.errors.empty());
}

TEST_CASE("Command line: --reconnect and --no-reconnect", "[reconnect]")
{
    CHECK_FALSE(Parse({"h"}).reconnect.has_value());
    CHECK(*Parse({"--reconnect", "h"}).reconnect);
    CHECK_FALSE(*Parse({"--no-reconnect", "h"}).reconnect);
}

TEST_CASE("Reconnect plan: profile value, flag override, default keepalive", "[reconnect]")
{
    std::vector<config::Profile> ps(1);
    ps[0].id = "cb2";
    ps[0].host = "10.0.0.108";
    ps[0].autoReconnect = true;

    auto plan = app::ResolveConnect(Parse({"cb2"}), ps);
    CHECK(plan.reconnect);
    CHECK(plan.keepAliveSec == app::kReconnectKeepAliveSec); // noticed even when TCP stays half-open

    auto off = app::ResolveConnect(Parse({"--no-reconnect", "cb2"}), ps);
    CHECK_FALSE(off.reconnect);
    CHECK(off.keepAliveSec == 0);

    auto noKeepAlive = app::ResolveConnect(Parse({"--keepalive", "0", "cb2"}), ps);
    CHECK(noKeepAlive.reconnect);
    CHECK(noKeepAlive.keepAliveSec == 0); // explicit 0 is respected

    ps[0].keepAliveSec = 30;
    CHECK(app::ResolveConnect(Parse({"cb2"}), ps).keepAliveSec == 30);

    auto quick = app::ResolveConnect(Parse({"--reconnect", "root@10.0.0.108"}), {});
    CHECK(quick.reconnect);
    CHECK(quick.keepAliveSec == app::kReconnectKeepAliveSec);
}

TEST_CASE("Saving with --reconnect stores autoReconnect, and the form keeps it", "[reconnect]")
{
    auto p = app::ProfileFromOptions(Parse({"--save", "cb2", "--reconnect", "root@10.0.0.108"}), nullptr);
    CHECK(p.autoReconnect);
    auto f = picker::FormFromProfile(p, false);
    CHECK(f.autoReconnect);
    CHECK(picker::ProfileFromForm(f, &p).autoReconnect);
}
