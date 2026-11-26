#include "app/ConnectPlan.h"
#include "cli/CommandLine.h"
#include "config/Profiles.h"
#include "config/SshConfigImport.h"
#include "util/Paths.h"
#include "vault/PasswordVault.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace winssh;
using config::HostKeyPolicy;
using config::Profile;

namespace
{
bool Contains(const std::vector<std::string> &v, const std::string &needle)
{
    for (const auto &s : v)
        if (s.find(needle) != std::string::npos)
            return true;
    return false;
}

cli::Options Parse(std::vector<std::string> args)
{
    auto r = cli::ParseCommandLine(args);
    REQUIRE(r.ok);
    return r.options;
}
} // namespace

TEST_CASE("Profiles: parse, defaults and validation", "[profiles]")
{
    auto pf = config::ParseProfiles(R"({
      "version": 1,
      "profiles": [
        { "id": "cb2-lab", "name": "CB2 lab", "host": "10.0.0.108", "user": "root",
          "savePassword": true, "highlight": ["default", "journal"], "keepAliveSec": 30,
          "hostKeyPolicy": "warn-on-change", "legacyAlgorithms": true,
          "autoReconnect": true, "wtTab": { "color": "#2d7d46" } },
        { "id": "fw", "host": "10.0.0.111", "port": 2222 },
        { "id": "bad id", "host": "x" },
        { "id": "nohost" },
        { "id": "CB2-LAB", "host": "dup" },
        { "id": "p", "host": "h", "hostKeyPolicy": "trust-me" }
      ]
    })");
    REQUIRE(pf.profiles.size() == 2);
    CHECK(pf.errors.size() == 4);
    CHECK(Contains(pf.errors, "'bad id'"));
    CHECK(Contains(pf.errors, "\"host\" is required"));
    CHECK(Contains(pf.errors, "duplicate id"));
    CHECK(Contains(pf.errors, "hostKeyPolicy"));

    const Profile &a = pf.profiles[0];
    CHECK(a.name == "CB2 lab");
    CHECK(a.port == 22);
    CHECK(a.savePassword);
    CHECK(a.highlight == std::vector<std::string>{"default", "journal"});
    CHECK(a.keepAliveSec == 30);
    CHECK(a.hostKeyPolicy == HostKeyPolicy::WarnOnChange);
    CHECK(a.legacyAlgorithms);
    CHECK(a.extraJson.find("autoReconnect") != std::string::npos); // kept for later milestones

    const Profile &b = pf.profiles[1];
    CHECK(b.name == "fw"); // defaults to id
    CHECK(b.port == 2222);
    CHECK(b.user.empty());
}

TEST_CASE("Profiles: serialize round trip keeps unknown fields", "[profiles]")
{
    auto pf = config::ParseProfiles(
        R"({"profiles": [{"id": "a", "host": "h", "user": "u", "autoReconnect": true, "keepAliveSec": 15}]})");
    REQUIRE(pf.profiles.size() == 1);
    const std::string text = config::SerializeProfiles(pf.profiles);
    auto again = config::ParseProfiles(text);
    REQUIRE(again.errors.empty());
    REQUIRE(again.profiles.size() == 1);
    CHECK(again.profiles[0].user == "u");
    CHECK(again.profiles[0].keepAliveSec == 15);
    CHECK(text.find("\"autoReconnect\": true") != std::string::npos);
    CHECK(text.find("savePassword") == std::string::npos); // defaults are not written
}

TEST_CASE("Profiles: invalid JSON is an error", "[profiles]")
{
    auto pf = config::ParseProfiles("{ nope");
    CHECK(pf.profiles.empty());
    CHECK(Contains(pf.errors, "not valid JSON"));
}

TEST_CASE("Profiles: ids and lookup", "[profiles]")
{
    CHECK(config::IsValidProfileId("cb2-lab.E13F8_x"));
    CHECK_FALSE(config::IsValidProfileId(""));
    CHECK_FALSE(config::IsValidProfileId("-x"));
    CHECK_FALSE(config::IsValidProfileId("a b"));
    CHECK_FALSE(config::IsValidProfileId("root@host"));
    CHECK_FALSE(config::IsValidProfileId(std::string(65, 'a')));

    std::vector<Profile> ps(1);
    ps[0].id = "CB2-Lab";
    CHECK(config::FindProfile(ps, "cb2-lab") == &ps[0]);
    CHECK(config::FindProfile(ps, "cb2") == nullptr);
}

TEST_CASE("History: most recent first, deduplicated, capped", "[profiles]")
{
    std::vector<config::RecentEntry> recent;
    config::RecordRecent(recent, {"root", "10.0.0.108", 22, "", "t1"});
    config::RecordRecent(recent, {"fc", "10.0.0.111", 22, "", "t2"});
    config::RecordRecent(recent, {"root", "10.0.0.108", 22, "", "t3"}); // same quick connect
    REQUIRE(recent.size() == 2);
    CHECK(recent[0].host == "10.0.0.108");
    CHECK(recent[0].lastConnected == "t3");

    config::RecordRecent(recent, {"root", "10.0.0.108", 22, "cb2-lab", "t4"}); // profile: separate entry
    CHECK(recent.size() == 3);
    config::RecordRecent(recent, {"root", "192.168.1.5", 22, "CB2-LAB", "t5"}); // same profile, new host
    CHECK(recent.size() == 3);
    CHECK(recent[0].host == "192.168.1.5");

    for (int i = 0; i < 30; ++i)
        config::RecordRecent(recent, {"u", "h" + std::to_string(i), 22, "", ""});
    CHECK(recent.size() == config::kMaxRecent);

    auto parsed = config::ParseHistory(config::SerializeHistory(recent));
    CHECK(parsed.size() == recent.size());
    CHECK(parsed[0].host == recent[0].host);
    CHECK(config::ParseHistory("garbage").empty());
}

TEST_CASE("NowUtcIso looks like ISO 8601 UTC", "[profiles]")
{
    const std::string t = config::NowUtcIso();
    REQUIRE(t.size() == 20);
    CHECK(t[4] == '-');
    CHECK(t[10] == 'T');
    CHECK(t.back() == 'Z');
}

TEST_CASE("ssh config import: hosts, wildcards and first value wins", "[profiles]")
{
    auto r = config::ParseSshConfig(R"(
# global
User defaultuser

Host cb2 cb2-alias
    HostName 10.0.0.108
    User root
    IdentityFile ~/.ssh/fc_ed25519

Host fw*
    Port 2222

Host fwserver
    HostName %h.lan
    Port 22

Host bad!name *.example.com
    User x

Match host foo
    User matched

Include other.conf

Host quoted
    HostName "10.0.0.5"
    Port=abc
)");
    CHECK(Contains(r.warnings, "Match"));
    CHECK(Contains(r.warnings, "Include"));
    CHECK(Contains(r.warnings, "invalid Port 'abc'"));

    auto find = [&](const char *id) -> const Profile * {
        for (const auto &p : r.profiles)
            if (p.id == id)
                return &p;
        return nullptr;
    };
    const Profile *cb2 = find("cb2");
    REQUIRE(cb2);
    CHECK(cb2->host == "10.0.0.108");
    CHECK(cb2->user == "defaultuser"); // global section comes first in the file: first value wins
    CHECK(cb2->identityFile == "~/.ssh/fc_ed25519");
    REQUIRE(find("cb2-alias"));

    const Profile *fw = find("fwserver");
    REQUIRE(fw);
    CHECK(fw->host == "fwserver.lan");
    CHECK(fw->port == 2222); // "Host fw*" appears before "Host fwserver"

    CHECK(find("quoted")->host == "10.0.0.5");
    CHECK(find("quoted")->port == 22);
    CHECK(find("*.example.com") == nullptr); // wildcard patterns are not profiles
}

TEST_CASE("ssh pattern matching", "[profiles]")
{
    CHECK(config::SshPatternMatch("fw*", "fwserver"));
    CHECK(config::SshPatternMatch("*", "anything"));
    CHECK(config::SshPatternMatch("10.0.0.?", "10.0.0.8"));
    CHECK_FALSE(config::SshPatternMatch("10.0.0.?", "10.0.0.18"));
    CHECK(config::SshPatternMatch("*.LAN", "box.lan"));
    CHECK_FALSE(config::SshPatternMatch("fw", "fwserver"));
}

TEST_CASE("ResolveConnect: profile values with flags on top", "[profiles]")
{
    std::vector<Profile> ps(1);
    ps[0].id = "cb2";
    ps[0].name = "CB2 lab";
    ps[0].host = "10.0.0.108";
    ps[0].user = "root";
    ps[0].port = 22;
    ps[0].highlight = {"default", "journal"};
    ps[0].keepAliveSec = 30;
    ps[0].hostKeyPolicy = HostKeyPolicy::WarnOnChange;

    auto plan = app::ResolveConnect(Parse({"cb2"}), ps);
    CHECK(plan.profileId == "cb2");
    CHECK(plan.host == "10.0.0.108");
    CHECK(plan.user == "root");
    CHECK(plan.highlightSets == std::vector<std::string>{"default", "journal"});
    CHECK(plan.keepAliveSec == 30);
    CHECK(plan.hostKeyPolicy == HostKeyPolicy::WarnOnChange);
    CHECK(plan.displayName == "CB2 lab");

    auto over = app::ResolveConnect(Parse({"-l", "fc", "-p", "2222", "--highlight", "x", "--keepalive", "0",
                                           "--host-key-policy", "strict", "CB2"}),
                                    ps);
    CHECK(over.profileId == "cb2");
    CHECK(over.user == "fc");
    CHECK(over.port == 2222);
    CHECK(over.highlightSets == std::vector<std::string>{"x"});
    CHECK(over.keepAliveSec == 0);
    CHECK(over.hostKeyPolicy == HostKeyPolicy::Strict);

    // With '@' it is a host, even if a profile has that name.
    auto host = app::ResolveConnect(Parse({"root@cb2"}), ps);
    CHECK(host.profileId.empty());
    CHECK(host.host == "cb2");

    // Unknown name: a host.
    auto unknown = app::ResolveConnect(Parse({"fwserver"}), ps);
    CHECK(unknown.profileId.empty());
    CHECK(unknown.host == "fwserver");
    CHECK(unknown.highlightSets == std::vector<std::string>{"default"});
}

TEST_CASE("ResolveConnect expands ~ in the key file", "[profiles]")
{
    auto plan = app::ResolveConnect(Parse({"-i", "~/.ssh/id_ed25519", "host"}), {});
    CHECK(plan.identityFile.find('~') == std::string::npos);
    CHECK(plan.identityFile.size() > std::string(".ssh/id_ed25519").size());
}

TEST_CASE("ProfileFromOptions builds and updates profiles", "[profiles]")
{
    auto p = app::ProfileFromOptions(
        Parse({"--save", "cb2", "--highlight", "default,journal", "--save-password", "root@10.0.0.108:2200"}),
        nullptr);
    CHECK(p.id == "cb2");
    CHECK(p.host == "10.0.0.108");
    CHECK(p.port == 2200);
    CHECK(p.user == "root");
    CHECK(p.savePassword);
    CHECK(p.highlight == std::vector<std::string>{"default", "journal"});

    p.keepAliveSec = 30;
    auto updated = app::ProfileFromOptions(Parse({"--save", "cb2", "10.0.0.109"}), &p);
    CHECK(updated.host == "10.0.0.109");
    CHECK(updated.user == "root");     // kept: not given this time
    CHECK(updated.keepAliveSec == 30); // kept
    CHECK(updated.savePassword);       // kept
}

TEST_CASE("ParseCommandLine: profile options", "[cli]")
{
    auto save = Parse({"--save", "cb2", "--save-password", "root@h"});
    CHECK(save.action == cli::Action::SaveProfile);
    CHECK(save.profileId == "cb2");
    CHECK(save.savePassword);
    CHECK(Parse({"--list"}).action == cli::Action::ListProfiles);
    CHECK(Parse({"--import-ssh-config"}).action == cli::Action::ImportSshConfig);
    auto del = Parse({"--delete", "cb2"});
    CHECK(del.action == cli::Action::DeleteProfile);
    CHECK(del.profileId == "cb2");

    auto plain = Parse({"cb2"});
    CHECK(plain.destinationMayBeProfile);
    CHECK_FALSE(Parse({"root@cb2"}).destinationMayBeProfile);
    CHECK_FALSE(Parse({"cb2:22"}).destinationMayBeProfile);

    CHECK_FALSE(cli::ParseCommandLine({"--save", "cb2"}).ok);              // needs a destination
    CHECK_FALSE(cli::ParseCommandLine({"--save-password", "host"}).ok);    // only with --save
    CHECK_FALSE(cli::ParseCommandLine({"--list", "host"}).ok);             // no destination
    CHECK_FALSE(cli::ParseCommandLine({"--delete"}).ok);
    CHECK_FALSE(cli::ParseCommandLine({"--keepalive", "-1", "h"}).ok);
    CHECK_FALSE(cli::ParseCommandLine({"--host-key-policy", "yolo", "h"}).ok);
    CHECK_FALSE(cli::ParseCommandLine({"--list", "--save", "a", "h"}).ok);
}

TEST_CASE("PasswordVault saves, loads and removes (test prefix)", "[vault]")
{
    // A test-only prefix so real "WinSSH:" entries are never touched.
    const std::string id = "unit-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const vault::PasswordVault v("WinSSH-test:");
    REQUIRE_FALSE(v.Exists(id));
    REQUIRE(v.Save(id, "root", "s3cr\xC3\xA9t pass"));
    CHECK(v.Exists(id));
    auto loaded = v.Load(id);
    REQUIRE(loaded.has_value());
    CHECK(*loaded == "s3cr\xC3\xA9t pass");
    vault::Wipe(*loaded);
    CHECK(loaded->empty());
    CHECK(v.Remove(id));
    CHECK_FALSE(v.Exists(id));
    CHECK_FALSE(v.Remove(id));
}
