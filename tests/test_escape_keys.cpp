#include "app/EscapeKeys.h"
#include "cli/CommandLine.h"

#include <catch2/catch_test_macros.hpp>

using winssh::app::Command;
using winssh::app::EscapeKeys;

TEST_CASE("EscapeKeys passes normal keys through", "[escape]")
{
    EscapeKeys e;
    auto r = e.Feed("ls -la\r\x03\x1b[A");
    CHECK(r.toRemote == "ls -la\r\x03\x1b[A");
    CHECK(r.commands.empty());
}

TEST_CASE("EscapeKeys runs commands after Ctrl+]", "[escape]")
{
    EscapeKeys e;
    auto r = e.Feed("a\x1d" "fb\x1dh\x1dX\x1d" "i");
    CHECK(r.toRemote == "ab");
    REQUIRE(r.commands.size() == 4);
    CHECK(r.commands[0] == Command::ToggleHighlight);
    CHECK(r.commands[1] == Command::Help);
    CHECK(r.commands[2] == Command::Exit); // upper case works
    CHECK(r.commands[3] == Command::Info);
}

TEST_CASE("EscapeKeys accepts Ctrl+letter after the escape key", "[escape]")
{
    EscapeKeys e;
    auto r = e.Feed("\x1d\x06\x1d\x18"); // Ctrl+F, Ctrl+X
    REQUIRE(r.commands.size() == 2);
    CHECK(r.commands[0] == Command::ToggleHighlight);
    CHECK(r.commands[1] == Command::Exit);
}

TEST_CASE("EscapeKeys sends the escape key when pressed twice", "[escape]")
{
    EscapeKeys e;
    auto r = e.Feed("\x1d\x1d");
    CHECK(r.toRemote == "\x1d");
    CHECK(r.commands.empty());
}

TEST_CASE("EscapeKeys keeps the armed state across reads", "[escape]")
{
    EscapeKeys e;
    auto r1 = e.Feed("x\x1d");
    CHECK(r1.toRemote == "x");
    CHECK(e.Armed());
    auto r2 = e.Feed("q");
    CHECK(r2.toRemote.empty());
    REQUIRE(r2.commands.size() == 1);
    CHECK(r2.commands[0] == Command::Unknown);
    CHECK_FALSE(e.Armed());
}

TEST_CASE("ParseCommandLine: --no-highlight", "[cli]")
{
    auto on = winssh::cli::ParseCommandLine({"host"});
    CHECK(on.options.highlight);
    auto off = winssh::cli::ParseCommandLine({"--no-highlight", "host"});
    REQUIRE(off.ok);
    CHECK_FALSE(off.options.highlight);
}
