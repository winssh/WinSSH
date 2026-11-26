// Highlight rules and their compiled form.
#pragma once

#include "highlight/Highlighter.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace winssh::highlight
{

struct Style
{
    // Named color ("red", "bright-cyan", ...), "#rrggbb", or "0".."255". Empty = unchanged.
    std::string fg;
    std::string bg;
    bool bold = false;
    bool underline = false;
    bool italic = false;
};

// Converts a style to an SGR sequence, e.g. {fg: "red", bold} -> "\x1b[1;31m".
// Returns std::nullopt and sets *error for an unknown color.
std::optional<std::string> ToSgr(const Style &style, std::string *error = nullptr);

struct Rule
{
    std::string name;
    std::vector<std::string> keywords; // whole words (or phrases), matched literally
    std::string regex;                 // used when keywords is empty (PCRE2 syntax)
    bool caseSensitive = false;
    bool enabled = true;
    Style style;
};

// Regex for one rule: keywords become \b(?:a|b|c)\b, case-insensitive rules get (?i:...).
std::string RulePattern(const Rule &rule);

// WinSerial's built-in keyword list, so highlighting looks the same out of the box.
std::vector<Rule> DefaultRules();

struct CompiledRules
{
    std::shared_ptr<const Matcher> matcher; // null when no rule compiled
    std::vector<std::string> styles;        // SGR per compiled rule, index = Match::rule
    std::vector<std::string> names;         // rule names, same order
    std::vector<std::string> errors;        // "rule 'x': message" for skipped rules
};

// Compiles all enabled rules into one PCRE2 pattern (one pass per text run, earlier rules win
// on overlap). Invalid rules are skipped and reported in `errors`.
CompiledRules CompileRules(const std::vector<Rule> &rules);

} // namespace winssh::highlight
