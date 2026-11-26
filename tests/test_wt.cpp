#include "cli/CommandLine.h"
#include "config/Profiles.h"
#include "wt/Fragment.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <regex>

using namespace winssh;
using nlohmann::json;

TEST_CASE("GuidV5 is stable, well-formed, version 5", "[wt]")
{
    const std::string a = wt::GuidV5("winssh:profile:cb2-lab");
    CHECK(a == wt::GuidV5("winssh:profile:cb2-lab"));
    CHECK(a != wt::GuidV5("winssh:profile:cb2-lab2"));
    CHECK(std::regex_match(a, std::regex(R"(\{[0-9a-f]{8}-[0-9a-f]{4}-5[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\})")));
}

TEST_CASE("ProfileGuid ignores id case and differs from the base profile", "[wt]")
{
    CHECK(wt::ProfileGuid("CB2-Lab") == wt::ProfileGuid("cb2-lab"));
    CHECK(wt::ProfileGuid("") != wt::ProfileGuid("cb2-lab"));
}

TEST_CASE("BuildFragment lists the picker and every profile", "[wt]")
{
    std::vector<config::Profile> ps(2);
    ps[0].id = "cb2-lab";
    ps[0].name = "CB2 lab unit";
    ps[0].host = "10.0.0.108";
    ps[0].extraJson = R"({"wtTab": {"color": "#2d7d46"}})";
    ps[1].id = "fw";
    ps[1].name = "fw";
    ps[1].host = "10.0.0.111";
    ps[1].extraJson = R"({"wtTab": {"color": "green"}})"; // invalid: ignored

    const auto j = json::parse(wt::BuildFragment(ps, std::filesystem::path(L"C:\\Program Files\\WinSSH\\WinSSH.exe")));
    const auto &list = j["profiles"];
    REQUIRE(list.size() == 3);

    CHECK(list[0]["name"] == "WinSSH");
    CHECK(list[0]["guid"] == wt::ProfileGuid(""));
    CHECK(list[0]["commandline"] == "\"C:\\Program Files\\WinSSH\\WinSSH.exe\"");
    CHECK(list[0]["icon"] == "C:\\Program Files\\WinSSH\\WinSSH.exe");

    CHECK(list[1]["name"] == "WinSSH: CB2 lab unit");
    CHECK(list[1]["guid"] == wt::ProfileGuid("cb2-lab"));
    CHECK(list[1]["commandline"] == "\"C:\\Program Files\\WinSSH\\WinSSH.exe\" cb2-lab");
    CHECK(list[1]["tabColor"] == "#2d7d46");

    CHECK(list[2]["name"] == "WinSSH: fw");
    CHECK_FALSE(list[2].contains("tabColor"));
}

TEST_CASE("ParseCommandLine: Windows Terminal options", "[cli]")
{
    CHECK(cli::ParseCommandLine({"--wt-install"}).options.action == cli::Action::WtInstall);
    CHECK(cli::ParseCommandLine({"--wt-uninstall"}).options.action == cli::Action::WtUninstall);
    CHECK_FALSE(cli::ParseCommandLine({"--wt-install", "host"}).ok);
    CHECK_FALSE(cli::ParseCommandLine({"--wt-install", "--list"}).ok);
}
