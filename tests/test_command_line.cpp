#include "cli/CommandLine.h"

#include <catch2/catch_test_macros.hpp>

using namespace winssh::cli;

namespace
{
Target MustParse(const char *text)
{
    std::string error;
    auto t = ParseTarget(text, &error);
    INFO("input: " << text << ", error: " << error);
    REQUIRE(t.has_value());
    return *t;
}

std::string ParseError(const char *text)
{
    std::string error;
    auto t = ParseTarget(text, &error);
    REQUIRE_FALSE(t.has_value());
    return error;
}
} // namespace

TEST_CASE("ParseTarget: host only", "[cli]")
{
    auto t = MustParse("10.0.0.108");
    CHECK(t.user.empty());
    CHECK(t.host == "10.0.0.108");
    CHECK_FALSE(t.port.has_value());
}

TEST_CASE("ParseTarget: user, host and port", "[cli]")
{
    auto t = MustParse("root@10.0.0.108:2222");
    CHECK(t.user == "root");
    CHECK(t.host == "10.0.0.108");
    CHECK(t.port == 2222);
}

TEST_CASE("ParseTarget: user containing '@' uses the last '@'", "[cli]")
{
    auto t = MustParse("hai@corp@build-server");
    CHECK(t.user == "hai@corp");
    CHECK(t.host == "build-server");
}

TEST_CASE("ParseTarget: IPv6", "[cli]")
{
    auto bare = MustParse("fe80::1");
    CHECK(bare.host == "fe80::1");
    CHECK_FALSE(bare.port.has_value());

    auto bracketed = MustParse("root@[fe80::1]:2222");
    CHECK(bracketed.user == "root");
    CHECK(bracketed.host == "fe80::1");
    CHECK(bracketed.port == 2222);

    auto noPort = MustParse("[::1]");
    CHECK(noPort.host == "::1");
    CHECK_FALSE(noPort.port.has_value());
}

TEST_CASE("ParseTarget: errors", "[cli]")
{
    CHECK(ParseError("") == "empty host name");
    CHECK(ParseError("root@") == "empty host name");
    CHECK(ParseError("@host") == "empty user name before '@'");
    CHECK(ParseError("host:") == "port must be a number from 1 to 65535");
    CHECK(ParseError("host:0") == "port must be a number from 1 to 65535");
    CHECK(ParseError("host:65536") == "port must be a number from 1 to 65535");
    CHECK(ParseError("host:22x") == "port must be a number from 1 to 65535");
    CHECK(ParseError("[::1") == "missing ']' in IPv6 address");
    CHECK(ParseError("[::1]x") == "unexpected text after ']'");
}

TEST_CASE("ParseCommandLine: no arguments opens the picker", "[cli]")
{
    auto r = ParseCommandLine({});
    REQUIRE(r.ok);
    CHECK(r.options.action == Action::Picker);
}

TEST_CASE("ParseCommandLine: help and version", "[cli]")
{
    CHECK(ParseCommandLine({"-h"}).options.action == Action::ShowHelp);
    CHECK(ParseCommandLine({"--help"}).options.action == Action::ShowHelp);
    CHECK(ParseCommandLine({"-V"}).options.action == Action::ShowVersion);
    CHECK(ParseCommandLine({"host", "--version"}).options.action == Action::ShowVersion);
}

TEST_CASE("ParseCommandLine: ssh-style flags", "[cli]")
{
    auto r = ParseCommandLine({"-p", "2222", "-l", "root", "-i", "C:\\keys\\id_ed25519", "10.0.0.108"});
    REQUIRE(r.ok);
    CHECK(r.options.action == Action::Connect);
    CHECK(r.options.host == "10.0.0.108");
    CHECK(r.options.user == "root");
    CHECK(r.options.port == 2222);
    CHECK(r.options.identityFile == "C:\\keys\\id_ed25519");
}

TEST_CASE("ParseCommandLine: default port", "[cli]")
{
    auto r = ParseCommandLine({"fc@10.0.0.111"});
    REQUIRE(r.ok);
    CHECK(r.options.port == kDefaultPort);
    CHECK(r.options.user == "fc");
}

TEST_CASE("ParseCommandLine: destination wins over flags", "[cli]")
{
    auto r = ParseCommandLine({"-l", "fc", "-p", "2222", "root@host:22"});
    REQUIRE(r.ok);
    CHECK(r.options.user == "root");
    CHECK(r.options.port == 22);
}

TEST_CASE("ParseCommandLine: errors", "[cli]")
{
    CHECK_FALSE(ParseCommandLine({"-p"}).ok);
    CHECK_FALSE(ParseCommandLine({"-p", "abc", "host"}).ok);
    CHECK_FALSE(ParseCommandLine({"-l"}).ok);
    CHECK_FALSE(ParseCommandLine({"-x", "host"}).ok);
    CHECK_FALSE(ParseCommandLine({"host", "uptime"}).ok);
    CHECK_FALSE(ParseCommandLine({"-l", "root"}).ok); // flag without destination
    CHECK_FALSE(ParseCommandLine({"root@"}).ok);
    CHECK(ParseCommandLine({"-x"}).error == "unknown option: -x");
}
