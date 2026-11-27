#include "app/ConnectPlan.h"
#include "app/EscapeKeys.h"
#include "cli/CommandLine.h"
#include "config/Profiles.h"
#include "log/LogText.h"
#include "log/SessionLog.h"
#include "picker/ProfileForm.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace winssh;
using log::LineAssembler;
using log::ScreenTimestamper;

namespace
{
std::string FakeTime()
{
    return "T";
}

std::string Feed(LineAssembler &a, std::string_view s)
{
    std::string out = a.Feed(s);
    return out;
}

const std::string TS = "\x1b[0;90m[T]\x1b[0m ";

cli::Options Parse(std::vector<std::string> args)
{
    auto r = cli::ParseCommandLine(args);
    REQUIRE(r.ok);
    return r.options;
}
} // namespace

TEST_CASE("Log text: escape sequences are dropped, lines get a time", "[log]")
{
    LineAssembler a(true, FakeTime);
    CHECK(Feed(a, "ab\x1b[31mcd\x1b[0m\r\nnext") == "[T] abcd\r\n");
    CHECK(a.Flush() == "[T] next\r\n");
    CHECK(a.Flush().empty());
}

TEST_CASE("Log text: titles and cursor moves are not text", "[log]")
{
    LineAssembler a(false, FakeTime);
    CHECK(Feed(a, "\x1b]0;root@cb2: ~\x07$ ls\x1b[K\r\n") == "$ ls\r\n");
}

TEST_CASE("Log text: a lone CR redraws the line (progress bars)", "[log]")
{
    LineAssembler a(false, FakeTime);
    CHECK(Feed(a, "10%\r50%\r100%\r\n") == "100%\r\n");
}

TEST_CASE("Log text: backspace removes a whole UTF-8 character", "[log]")
{
    LineAssembler a(false, FakeTime);
    CHECK(Feed(a, "lsx\b \b\r\n") == "ls\r\n"); // "\b \b" is how a terminal erases a character
    CHECK(Feed(a, "thi\xE1\xBA\xBF\b\r\n") == "thi\r\n");
}

TEST_CASE("Log text: empty lines are kept", "[log]")
{
    LineAssembler a(true, FakeTime);
    CHECK(Feed(a, "a\r\n\r\nb\r\n") == "[T] a\r\n[T] \r\n[T] b\r\n");
}

TEST_CASE("Log text: full-screen apps are not logged, only noted", "[log]")
{
    LineAssembler a(true, FakeTime);
    const std::string out = Feed(a, "before\r\n\x1b[?1049h\x1b[H\x1b[2Jvim screen\r\nmore\x1b[?1049l$ ");
    CHECK(out == "[T] before\r\n[T] --- full-screen app opened (not logged) ---\r\n"
                 "[T] --- full-screen app closed ---\r\n");
    CHECK(a.Flush() == "[T] $ \r\n");
}

TEST_CASE("Log text: markers end the current line first", "[log]")
{
    LineAssembler a(true, FakeTime);
    CHECK(Feed(a, "half").empty());
    CHECK(a.Marker("--- reconnected ---") == "[T] half\r\n[T] --- reconnected ---\r\n");
    a.SetTimestamps(false);
    CHECK(a.Marker("x") == "x\r\n");
}

TEST_CASE("NowTimeText is HH:MM:SS.mmm", "[log]")
{
    const std::string t = log::NowTimeText();
    REQUIRE(t.size() == 12);
    CHECK(t[2] == ':');
    CHECK(t[5] == ':');
    CHECK(t[8] == '.');
}

TEST_CASE("Screen timestamps: prefix each line when enabled", "[log]")
{
    ScreenTimestamper s(FakeTime);
    CHECK(s.Process("a\r\nb") == "a\r\nb"); // off: unchanged, state still tracked
    s.SetEnabled(true);
    CHECK(s.Process("c\r\nd") == "c\r\n" + TS + "d"); // switched on mid-line: from the next line
    CHECK(s.Process("\r\n\x1b[34mblue\r\n") == "\r\n\x1b[34m" + TS + "\x1b[34mblue\r\n");
}

TEST_CASE("Screen timestamps: keep the server's color after the prefix", "[log]")
{
    ScreenTimestamper s(FakeTime);
    s.SetEnabled(true);
    CHECK(s.Process("\x1b[1;32mok\r\nstill green") ==
          "\x1b[1;32m" + TS + "\x1b[1;32mok\r\n" + TS + "\x1b[1;32mstill green");
}

TEST_CASE("Screen timestamps: never inside a full-screen app", "[log]")
{
    ScreenTimestamper s(FakeTime);
    s.SetEnabled(true);
    CHECK(s.Process("\x1b[?1049hx\r\ny\r\n") == "\x1b[?1049hx\r\ny\r\n");
    CHECK(s.InAltScreen());
    CHECK(s.Process("\x1b[?1049l$ ") == "\x1b[?1049l" + TS + "$ ");
    CHECK_FALSE(s.InAltScreen());
    s.Process("\x1b[?1049h");
    s.Reset();
    CHECK_FALSE(s.InAltScreen());
    CHECK(s.Process("z") == TS + "z");
}

TEST_CASE("SessionLog writes a plain text file with markers", "[log]")
{
    const auto dir = std::filesystem::temp_directory_path() / "winssh-tests" / "logs";
    std::filesystem::remove_all(dir);
    log::SessionLog l("cb2/lab", "root@10.0.0.115 (profile cb2)", false);
    CHECK_FALSE(l.IsOn());
    l.Write("ignored while off\r\n");
    std::string error;
    REQUIRE(l.Start(&error, dir));
    CHECK(l.IsOn());
    const auto path = l.Path();
    CHECK(path.filename().string().rfind("cb2_lab-", 0) == 0); // file-safe name
    CHECK(path.extension() == ".log");
    l.Write("\x1b[31merror\x1b[0m: x\r\npartial");
    l.Marker("connection lost");
    l.Write("after\r\n");
    l.Stop();
    CHECK_FALSE(l.IsOn());

    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();
    CHECK(text.rfind("# WinSSH session log: root@10.0.0.115 (profile cb2), started ", 0) == 0);
    CHECK(text.find("error: x\r\npartial\r\n--- connection lost ---\r\nafter\r\n") != std::string::npos);
    CHECK(text.find("ignored while off") == std::string::npos);
    CHECK(text.find("# log closed ") != std::string::npos);
    CHECK(text.find('\x1b') == std::string::npos);

    // A second log in the same second gets its own file.
    log::SessionLog l2("cb2/lab", "t", false);
    REQUIRE(l2.Start(&error, dir));
    log::SessionLog l3("cb2/lab", "t", false);
    REQUIRE(l3.Start(&error, dir));
    CHECK(l2.Path() != l3.Path());
}

TEST_CASE("Ctrl+] l and Ctrl+] t", "[log]")
{
    app::EscapeKeys e;
    auto r = e.Feed("\x1dl\x1dt");
    REQUIRE(r.commands.size() == 2);
    CHECK(r.commands[0] == app::Command::ToggleLog);
    CHECK(r.commands[1] == app::Command::ToggleTimestamps);
}

TEST_CASE("Log options: profile, flags, plan", "[log]")
{
    auto pf = config::ParseProfiles(
        R"({"profiles": [{"id": "cb2", "host": "h", "log": {"enabled": true, "timestamps": false}},
                         {"id": "x", "host": "h", "log": "yes"}]})");
    REQUIRE(pf.profiles.size() == 1);
    CHECK(pf.profiles[0].logEnabled);
    CHECK_FALSE(pf.profiles[0].logTimestamps);
    CHECK(pf.errors.size() == 1);
    CHECK(config::SerializeProfiles(pf.profiles).find("\"timestamps\": false") != std::string::npos);

    CHECK(*Parse({"--log", "h"}).log);
    CHECK_FALSE(*Parse({"--no-log", "h"}).log);
    CHECK(Parse({"--timestamps", "h"}).screenTimestamps);

    auto plan = app::ResolveConnect(Parse({"cb2"}), pf.profiles);
    CHECK(plan.logEnabled);
    CHECK_FALSE(plan.logTimestamps);
    CHECK_FALSE(app::ResolveConnect(Parse({"--no-log", "cb2"}), pf.profiles).logEnabled);
    CHECK(app::ResolveConnect(Parse({"--timestamps", "h"}), {}).screenTimestamps);
    CHECK(app::ProfileFromOptions(Parse({"--save", "y", "--log", "h"}), nullptr).logEnabled);
}

TEST_CASE("Timestamps: on screen (default off) and in the log file (default on) are separate", "[log]")
{
    // Defaults
    config::Profile none;
    CHECK_FALSE(none.screenTimestamps);
    CHECK(none.logTimestamps);
    auto plain = app::ResolveConnect(Parse({"h"}), {});
    CHECK_FALSE(plain.screenTimestamps);
    CHECK(plain.logTimestamps);

    // Profile fields
    auto pf = config::ParseProfiles(
        R"({"profiles": [{"id": "cb2", "host": "h", "screenTimestamps": true, "log": {"enabled": true}}]})");
    REQUIRE(pf.profiles.size() == 1);
    CHECK(pf.profiles[0].screenTimestamps);
    CHECK(pf.profiles[0].logTimestamps);
    const std::string text = config::SerializeProfiles(pf.profiles);
    CHECK(text.find("\"screenTimestamps\": true") != std::string::npos);
    auto plan = app::ResolveConnect(Parse({"cb2"}), pf.profiles);
    CHECK(plan.screenTimestamps);
    CHECK(plan.logTimestamps);

    // Flags: --timestamps is the screen, --log-no-timestamps the file
    auto flags = app::ResolveConnect(Parse({"--log", "--log-no-timestamps", "h"}), {});
    CHECK_FALSE(flags.screenTimestamps);
    CHECK_FALSE(flags.logTimestamps);
    auto both = app::ResolveConnect(Parse({"--timestamps", "--log-no-timestamps", "cb2"}), pf.profiles);
    CHECK(both.screenTimestamps);
    CHECK_FALSE(both.logTimestamps);

    // --save keeps them; the dialog form round-trips them
    auto saved = app::ProfileFromOptions(Parse({"--save", "z", "--timestamps", "--log-no-timestamps", "h"}), nullptr);
    CHECK(saved.screenTimestamps);
    CHECK_FALSE(saved.logTimestamps);
    auto form = picker::FormFromProfile(saved, false);
    CHECK(form.screenTimestamps);
    CHECK_FALSE(form.logTimestamps);
    auto back = picker::ProfileFromForm(form, &saved);
    CHECK(back.screenTimestamps);
    CHECK_FALSE(back.logTimestamps);
}
