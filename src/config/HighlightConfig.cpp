#include "config/HighlightConfig.h"

#include "util/Paths.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <sstream>

namespace winssh::config
{
namespace
{
using nlohmann::json;
using highlight::Rule;

void AddBuiltins(RuleSets &rs)
{
    if (rs.sets.count(kDefaultRuleSet) == 0)
    {
        rs.sets[kDefaultRuleSet] = highlight::DefaultRules();
        rs.order.insert(rs.order.begin(), kDefaultRuleSet);
    }
}

std::string TypeName(const json &j)
{
    return j.type_name();
}

// Parses one rule object. Returns false (and adds an error) when the rule must be skipped.
bool ParseRule(const json &j, const std::string &where, Rule &rule, HighlightConfig &cfg)
{
    if (!j.is_object())
    {
        cfg.errors.push_back(where + ": expected an object, got " + TypeName(j));
        return false;
    }
    static const std::set<std::string> kKnown = {"name", "keywords", "regex", "caseSensitive", "enabled", "style"};
    for (auto it = j.begin(); it != j.end(); ++it)
        if (kKnown.count(it.key()) == 0)
            cfg.warnings.push_back(where + ": unknown field '" + it.key() + "' (ignored)");

    auto fail = [&](const std::string &msg) {
        cfg.errors.push_back(where + (rule.name.empty() ? "" : " ('" + rule.name + "')") + ": " + msg);
        return false;
    };

    if (j.contains("name"))
    {
        if (!j["name"].is_string())
            return fail("\"name\" must be a string");
        rule.name = j["name"].get<std::string>();
    }
    if (j.contains("keywords"))
    {
        const json &k = j["keywords"];
        if (!k.is_array())
            return fail("\"keywords\" must be an array of strings");
        for (const json &w : k)
        {
            if (!w.is_string() || w.get<std::string>().empty())
                return fail("\"keywords\" must contain non-empty strings");
            rule.keywords.push_back(w.get<std::string>());
        }
    }
    if (j.contains("regex"))
    {
        if (!j["regex"].is_string())
            return fail("\"regex\" must be a string");
        rule.regex = j["regex"].get<std::string>();
    }
    if (!rule.keywords.empty() && !rule.regex.empty())
        return fail("use either \"keywords\" or \"regex\", not both");
    if (rule.keywords.empty() && rule.regex.empty())
        return fail("needs \"keywords\" or \"regex\"");
    for (const char *flag : {"caseSensitive", "enabled"})
    {
        if (j.contains(flag) && !j[flag].is_boolean())
            return fail(std::string("\"") + flag + "\" must be true or false");
    }
    rule.caseSensitive = j.value("caseSensitive", false);
    rule.enabled = j.value("enabled", true);

    if (j.contains("style"))
    {
        const json &s = j["style"];
        if (s.is_string())
        {
            rule.style.fg = s.get<std::string>(); // shorthand: "style": "red"
        }
        else if (s.is_object())
        {
            static const std::set<std::string> kStyleKeys = {"fg", "bg", "bold", "underline", "italic"};
            for (auto it = s.begin(); it != s.end(); ++it)
                if (kStyleKeys.count(it.key()) == 0)
                    cfg.warnings.push_back(where + ".style: unknown field '" + it.key() + "' (ignored)");
            for (const char *c : {"fg", "bg"})
                if (s.contains(c) && !s[c].is_string() && !s[c].is_number_integer())
                    return fail(std::string("\"style.") + c + "\" must be a color name, \"#rrggbb\" or 0-255");
            auto color = [&](const char *key) -> std::string {
                if (!s.contains(key))
                    return {};
                return s[key].is_number_integer() ? std::to_string(s[key].get<int>()) : s[key].get<std::string>();
            };
            rule.style.fg = color("fg");
            rule.style.bg = color("bg");
            for (const char *b : {"bold", "underline", "italic"})
                if (s.contains(b) && !s[b].is_boolean())
                    return fail(std::string("\"style.") + b + "\" must be true or false");
            rule.style.bold = s.value("bold", false);
            rule.style.underline = s.value("underline", false);
            rule.style.italic = s.value("italic", false);
        }
        else
        {
            return fail("\"style\" must be a color name or an object");
        }
        std::string colorError;
        if (!highlight::ToSgr(rule.style, &colorError))
            return fail(colorError);
    }
    return true;
}

} // namespace

HighlightConfig ParseHighlightConfig(std::string_view text)
{
    HighlightConfig cfg;
    cfg.fileFound = true;

    json root;
    try
    {
        root = json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/true);
    }
    catch (const json::parse_error &e)
    {
        cfg.errors.push_back(std::string("not valid JSON: ") + e.what() + " (using built-in rules)");
        AddBuiltins(cfg.ruleSets);
        return cfg;
    }

    if (!root.is_object())
    {
        cfg.errors.push_back("top level must be an object with \"ruleSets\" (using built-in rules)");
        AddBuiltins(cfg.ruleSets);
        return cfg;
    }
    for (auto it = root.begin(); it != root.end(); ++it)
        if (it.key() != "ruleSets" && it.key() != "$schema" && it.key() != "version")
            cfg.warnings.push_back("unknown top-level field '" + it.key() + "' (ignored)");

    if (root.contains("ruleSets"))
    {
        const json &sets = root["ruleSets"];
        if (!sets.is_object())
        {
            cfg.errors.push_back("\"ruleSets\" must be an object of named arrays");
        }
        else
        {
            for (auto it = sets.begin(); it != sets.end(); ++it)
            {
                const std::string &setName = it.key();
                const std::string where = "ruleSets." + setName;
                if (!it.value().is_array())
                {
                    cfg.errors.push_back(where + ": expected an array of rules, got " + TypeName(it.value()));
                    continue;
                }
                std::vector<Rule> rules;
                size_t index = 0;
                for (const json &r : it.value())
                {
                    Rule rule;
                    const std::string rwhere = where + "[" + std::to_string(index) + "]";
                    if (ParseRule(r, rwhere, rule, cfg))
                    {
                        if (rule.name.empty())
                            rule.name = setName + "[" + std::to_string(index) + "]";
                        rules.push_back(std::move(rule));
                    }
                    ++index;
                }
                cfg.ruleSets.sets[setName] = std::move(rules);
                cfg.ruleSets.order.push_back(setName);
            }
        }
    }
    AddBuiltins(cfg.ruleSets);
    return cfg;
}

HighlightConfig LoadHighlightConfig(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        HighlightConfig cfg;
        AddBuiltins(cfg.ruleSets);
        return cfg;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) // UTF-8 BOM (Notepad)
        text.erase(0, 3);
    return ParseHighlightConfig(text);
}

std::filesystem::path HighlightConfigPath()
{
    return util::ConfigDir() / L"highlight.json";
}

Selection SelectRuleSets(const RuleSets &ruleSets, const std::vector<std::string> &names)
{
    Selection sel;
    for (const auto &name : names)
    {
        auto it = ruleSets.sets.find(name);
        if (it == ruleSets.sets.end())
        {
            std::string known;
            for (const auto &n : ruleSets.order)
                known += (known.empty() ? "" : ", ") + n;
            sel.warnings.push_back("unknown rule set '" + name + "' (available: " + known + ")");
            continue;
        }
        sel.rules.insert(sel.rules.end(), it->second.begin(), it->second.end());
    }
    return sel;
}

std::string StarterConfig()
{
    return R"({
  // WinSSH highlight rules. Format: https://github.com/winssh/WinSSH/blob/main/docs/highlight-rules.md
  // WinSSH reloads this file while a session is open. Check it with: winssh --check-rules
  //
  // Rule fields: "name", "keywords" (whole words) or "regex" (PCRE2), "caseSensitive" (default false),
  // "enabled" (default true), "style": "red" or { "fg", "bg", "bold", "underline", "italic" }.
  // Colors: black red green yellow blue magenta cyan white, bright-<color>, gray, "#rrggbb", 0-255.
  // Earlier rules win when two rules match the same text.
  "ruleSets": {
    // Used when no --highlight option is given. Defining "default" here replaces the built-in one.
    "default": [
      { "name": "errors", "keywords": ["error", "failed", "no", "down", "disabled", "unknown", "fault",
                                       "shutdown", "disconnected", "denied", "refused", "problem", "failure"],
        "style": "red" },
      { "name": "success", "keywords": ["enabled", "connected", "up", "yes", "ok", "success"], "style": "green" },
      { "name": "ipv4", "regex": "\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b", "style": "yellow" },
      { "name": "mac", "regex": "\\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\\b", "style": "yellow" },
      { "name": "network", "keywords": ["localhost", "vlan"], "style": "yellow" },
      { "name": "config", "keywords": ["ip address", "show", "interface", "route", "spanning-tree"], "style": "cyan" }
    ],

    // Example extra set: winssh --highlight default,journal user@host
    "journal": [
      { "name": "priority-error", "regex": "\\b(ERR|CRIT|ALERT|EMERG)\\b", "caseSensitive": true,
        "style": { "fg": "white", "bg": "red", "bold": true } },
      { "name": "systemd-unit", "regex": "\\b[\\w@.-]+\\.service\\b", "style": "bright-blue" }
    ]
  }
}
)";
}

} // namespace winssh::config
