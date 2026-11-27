#include "highlight/Highlighter.h"
#include "highlight/Rules.h"

#include <catch2/catch_test_macros.hpp>

using namespace winssh::highlight;

namespace
{
const std::string RST = "\x1b[0m";

std::string Run(const CompiledRules &c, std::string_view text)
{
    Highlighter h(c.matcher, c.styles);
    std::string out = h.Process(text); // separate statements: operand order of + is unspecified
    out += h.Flush();
    return out;
}
} // namespace

TEST_CASE("ToSgr builds SGR sequences", "[rules]")
{
    Style s;
    CHECK(ToSgr(s) == "");
    s.fg = "red";
    CHECK(ToSgr(s) == "\x1b[31m");
    s.bold = true;
    s.bg = "bright-blue";
    CHECK(ToSgr(s) == "\x1b[1;31;104m");
    Style rgb;
    rgb.fg = "#ff8000";
    CHECK(ToSgr(rgb) == "\x1b[38;2;255;128;0m");
    Style idx;
    idx.bg = "242";
    CHECK(ToSgr(idx) == "\x1b[48;5;242m");
    Style bad;
    bad.fg = "rouge";
    std::string error;
    CHECK_FALSE(ToSgr(bad, &error).has_value());
    CHECK(error.find("rouge") != std::string::npos);
}

TEST_CASE("RulePattern escapes keywords and applies case sensitivity", "[rules]")
{
    Rule r;
    r.keywords = {"a.b", "c+"};
    CHECK(RulePattern(r) == R"((?i:\b(?:a\.b|c\+)\b))");
    r.caseSensitive = true;
    CHECK(RulePattern(r) == R"((?:\b(?:a\.b|c\+)\b))");
}

TEST_CASE("Default rules match WinSerial's keywords", "[rules]")
{
    const auto c = CompileRules(DefaultRules());
    REQUIRE(c.errors.empty());
    REQUIRE(c.matcher);
    const std::string RED = "\x1b[31m", GREEN = "\x1b[32m", YELLOW = "\x1b[33m", CYAN = "\x1b[36m";

    CHECK(Run(c, "Link is Down") == "Link is " + RED + "Down" + RST);
    CHECK(Run(c, "status: OK") == "status: " + GREEN + "OK" + RST);
    CHECK(Run(c, "addr 10.0.0.108 mac A4:5E:60:01:02:03") ==
          "addr " + YELLOW + "10.0.0.108" + RST + " mac " + YELLOW + "A4:5E:60:01:02:03" + RST);
    CHECK(Run(c, "show ip address") == CYAN + "show" + RST + " " + CYAN + "ip address" + RST);
    CHECK(Run(c, "errors upgrade noise") == "errors upgrade noise"); // whole words only
}

TEST_CASE("User regex with its own groups maps to the right rule", "[rules]")
{
    Rule a;
    a.name = "kv";
    a.regex = R"((\w+)=(\d+))";
    a.style.fg = "blue";
    Rule b;
    b.name = "fc";
    b.regex = R"(\bfc-[a-z]+\b)";
    b.caseSensitive = true;
    b.style.fg = "cyan";
    const auto c = CompileRules({a, b});
    REQUIRE(c.errors.empty());
    CHECK(Run(c, "count=5 fc-io FC-IO") ==
          "\x1b[34mcount=5" + RST + " \x1b[36mfc-io" + RST + " FC-IO");
}

TEST_CASE("Invalid rules are skipped with an error, others still work", "[rules]")
{
    Rule bad;
    bad.name = "broken";
    bad.regex = "(unclosed";
    Rule color;
    color.name = "color";
    color.keywords = {"x"};
    color.style.fg = "rouge";
    Rule empty;
    empty.name = "empty";
    Rule good;
    good.name = "good";
    good.keywords = {"ok"};
    good.style.fg = "green";
    const auto c = CompileRules({bad, color, empty, good});
    REQUIRE(c.errors.size() == 3);
    CHECK(c.errors[0].rfind("rule 'broken': ", 0) == 0);
    CHECK(c.names == std::vector<std::string>{"good"});
    CHECK(Run(c, "ok") == "\x1b[32mok" + RST);
}

TEST_CASE("Earlier rules win on overlap", "[rules]")
{
    Rule first;
    first.name = "first";
    first.keywords = {"link down"};
    first.style.fg = "magenta";
    Rule second;
    second.name = "second";
    second.keywords = {"down"};
    second.style.fg = "red";
    const auto c = CompileRules({first, second});
    CHECK(Run(c, "link down") == "\x1b[35mlink down" + RST);
}

TEST_CASE("A catastrophic regex does not hang", "[rules]")
{
    Rule bad;
    bad.name = "slow";
    bad.regex = "(a+)+$";
    bad.style.fg = "red";
    const auto c = CompileRules({bad});
    const std::string input = std::string(40, 'a') + "b";
    CHECK(Run(c, input) == input); // match limit hit: left plain, returns quickly
}

TEST_CASE("Invalid UTF-8 in output does not break matching", "[rules]")
{
    const auto c = CompileRules(DefaultRules());
    CHECK(Run(c, "\xFF\xFE error") == "\xFF\xFE \x1b[31merror" + RST);
}
