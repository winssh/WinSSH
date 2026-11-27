#include "app/RulesSource.h"
#include "cli/CommandLine.h"
#include "config/HighlightConfig.h"
#include "highlight/Highlighter.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace winssh;
using config::ParseHighlightConfig;

namespace
{
bool Contains(const std::vector<std::string> &v, const std::string &needle)
{
    for (const auto &s : v)
        if (s.find(needle) != std::string::npos)
            return true;
    return false;
}

std::filesystem::path TempFile(const char *name)
{
    auto dir = std::filesystem::temp_directory_path() / "winssh-tests";
    std::filesystem::create_directories(dir);
    return dir / name;
}
} // namespace

TEST_CASE("Config: empty object keeps the built-in default set", "[config]")
{
    auto cfg = ParseHighlightConfig("{}");
    CHECK(cfg.errors.empty());
    REQUIRE(cfg.ruleSets.sets.count("default") == 1);
    CHECK(cfg.ruleSets.sets.at("default").size() == highlight::DefaultRules().size());
}

TEST_CASE("Config: parses rule sets with comments and both style forms", "[config]")
{
    auto cfg = ParseHighlightConfig(R"({
      // comment
      "ruleSets": {
        "cb2": [
          { "name": "fc", "regex": "\\bfc-[a-z]+\\b", "caseSensitive": true, "style": "cyan" },
          { "name": "err", "keywords": ["ERR", "CRIT"], "style": { "fg": "white", "bg": "red", "bold": true } },
          { "keywords": ["x"], "style": { "fg": 208 }, "enabled": false }
        ]
      }
    })");
    REQUIRE(cfg.errors.empty());
    CHECK(cfg.warnings.empty());
    const auto &cb2 = cfg.ruleSets.sets.at("cb2");
    REQUIRE(cb2.size() == 3);
    CHECK(cb2[0].regex == "\\bfc-[a-z]+\\b");
    CHECK(cb2[0].caseSensitive);
    CHECK(cb2[0].style.fg == "cyan");
    CHECK(cb2[1].keywords == std::vector<std::string>{"ERR", "CRIT"});
    CHECK(cb2[1].style.bg == "red");
    CHECK(cb2[1].style.bold);
    CHECK(cb2[2].name == "cb2[2]"); // generated name
    CHECK(cb2[2].style.fg == "208");
    CHECK_FALSE(cb2[2].enabled);
    // "default" still available, listed first
    CHECK(cfg.ruleSets.order == std::vector<std::string>{"default", "cb2"});
}

TEST_CASE("Config: a file 'default' replaces the built-in one", "[config]")
{
    auto cfg = ParseHighlightConfig(R"({"ruleSets": {"default": [ {"name": "only", "keywords": ["x"]} ]}})");
    REQUIRE(cfg.ruleSets.sets.at("default").size() == 1);
    CHECK(cfg.ruleSets.sets.at("default")[0].name == "only");
}

TEST_CASE("Config: bad rules are skipped with a located error", "[config]")
{
    auto cfg = ParseHighlightConfig(R"({"ruleSets": {"s": [
        {"name": "both", "keywords": ["a"], "regex": "b"},
        {"name": "none"},
        {"name": "color", "keywords": ["a"], "style": "rouge"},
        {"name": "type", "keywords": "a"},
        42,
        {"name": "good", "keywords": ["ok"]}
    ]}})");
    CHECK(cfg.errors.size() == 5);
    CHECK(Contains(cfg.errors, "ruleSets.s[0] ('both'): use either"));
    CHECK(Contains(cfg.errors, "ruleSets.s[1] ('none'): needs"));
    CHECK(Contains(cfg.errors, "unknown color 'rouge'"));
    CHECK(Contains(cfg.errors, "\"keywords\" must be an array"));
    CHECK(Contains(cfg.errors, "ruleSets.s[4]: expected an object"));
    REQUIRE(cfg.ruleSets.sets.at("s").size() == 1);
    CHECK(cfg.ruleSets.sets.at("s")[0].name == "good");
}

TEST_CASE("Config: unknown fields are warnings, not errors", "[config]")
{
    auto cfg = ParseHighlightConfig(R"({"rulesets": {}, "ruleSets": {"s": [{"keywords": ["a"], "colour": "red"}]}})");
    CHECK(cfg.errors.empty());
    CHECK(Contains(cfg.warnings, "unknown top-level field 'rulesets'"));
    CHECK(Contains(cfg.warnings, "unknown field 'colour'"));
}

TEST_CASE("Config: invalid JSON falls back to built-in rules", "[config]")
{
    auto cfg = ParseHighlightConfig("{ \"ruleSets\": { ");
    REQUIRE(cfg.errors.size() == 1);
    CHECK(Contains(cfg.errors, "not valid JSON"));
    CHECK(cfg.ruleSets.sets.count("default") == 1);
}

TEST_CASE("Config: the starter file is valid and has no warnings", "[config]")
{
    auto cfg = ParseHighlightConfig(config::StarterConfig());
    CHECK(cfg.errors.empty());
    CHECK(cfg.warnings.empty());
    CHECK(cfg.ruleSets.sets.count("journal") == 1);
    const auto compiled = highlight::CompileRules(cfg.ruleSets.sets.at("journal"));
    CHECK(compiled.errors.empty());
}

TEST_CASE("Config: selecting sets keeps order and reports unknown names", "[config]")
{
    auto cfg = ParseHighlightConfig(R"({"ruleSets": {"a": [{"name": "a1", "keywords": ["x"]}],
                                                       "b": [{"name": "b1", "keywords": ["y"]}]}})");
    auto sel = config::SelectRuleSets(cfg.ruleSets, {"b", "nope", "a"});
    REQUIRE(sel.rules.size() == 2);
    CHECK(sel.rules[0].name == "b1");
    CHECK(sel.rules[1].name == "a1");
    REQUIRE(sel.warnings.size() == 1);
    CHECK(Contains(sel.warnings, "unknown rule set 'nope'"));
}

TEST_CASE("Config: missing file means built-in rules, not an error", "[config]")
{
    auto cfg = config::LoadHighlightConfig(TempFile("does-not-exist.json"));
    CHECK_FALSE(cfg.fileFound);
    CHECK(cfg.errors.empty());
    CHECK(cfg.ruleSets.sets.count("default") == 1);
}

TEST_CASE("RulesSource notices file changes", "[config]")
{
    const auto path = TempFile("rules-source.json");
    std::filesystem::remove(path);
    app::RulesSource source(path, {"s"});
    auto first = source.Load();
    CHECK_FALSE(first.fileFound);
    CHECK(Contains(first.messages, "unknown rule set 's'"));
    CHECK_FALSE(source.Changed());

    {
        std::ofstream(path) << R"({"ruleSets": {"s": [{"name": "r", "keywords": ["x"], "style": "red"}]}})";
    }
    CHECK(source.Changed());
    auto second = source.Load();
    CHECK(second.fileFound);
    CHECK(second.ruleCount == 1);
    CHECK_FALSE(source.Changed());

    std::filesystem::remove(path);
    CHECK(source.Changed());
}

TEST_CASE("Highlighter::SetRules swaps rules", "[highlight]")
{
    highlight::Rule r;
    r.name = "x";
    r.keywords = {"beta"};
    r.style.fg = "red";
    const auto c = highlight::CompileRules({r});
    highlight::Highlighter h(nullptr, {});
    std::string out = h.Process("beta\r\n");
    CHECK(out == "beta\r\n");
    h.SetRules(c.matcher, c.styles);
    out = h.Process("beta\r\n");
    CHECK(out == "\x1b[31mbeta\x1b[0m\r\n");
}

TEST_CASE("ParseCommandLine: rules options", "[cli]")
{
    auto sets = cli::ParseCommandLine({"--highlight", "default, journal,,", "host"});
    REQUIRE(sets.ok);
    CHECK(sets.options.highlightSets == std::vector<std::string>{"default", "journal"});
    CHECK(cli::ParseCommandLine({"host"}).options.highlightSets == std::vector<std::string>{"default"});

    CHECK(cli::ParseCommandLine({"--check-rules"}).options.action == cli::Action::CheckRules);
    CHECK(cli::ParseCommandLine({"--init-rules"}).options.action == cli::Action::InitRules);
    auto t = cli::ParseCommandLine({"--test-rules", "link down", "--highlight", "journal"});
    REQUIRE(t.ok);
    CHECK(t.options.action == cli::Action::TestRules);
    CHECK(t.options.testText == "link down");
    CHECK(t.options.highlightSets == std::vector<std::string>{"journal"});

    CHECK_FALSE(cli::ParseCommandLine({"--highlight"}).ok);
    CHECK_FALSE(cli::ParseCommandLine({"--highlight", " , "}).ok);
    CHECK_FALSE(cli::ParseCommandLine({"--test-rules"}).ok);
    CHECK_FALSE(cli::ParseCommandLine({"--check-rules", "host"}).ok);
    CHECK_FALSE(cli::ParseCommandLine({"--check-rules", "--init-rules"}).ok);
}
