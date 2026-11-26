#include "app/RulesCommands.h"

#include "app/RulesSource.h"
#include "config/HighlightConfig.h"
#include "console/Console.h"
#include "highlight/Highlighter.h"
#include "util/Utf8.h"

#include <windows.h>

#include <filesystem>
#include <fstream>

namespace winssh::app
{
namespace
{
namespace fs = std::filesystem;

bool g_plain = false; // strip colors (stdout redirected to a file or pipe)

bool StdoutIsConsole()
{
    DWORD mode = 0;
    return ::GetConsoleMode(::GetStdHandle(STD_OUTPUT_HANDLE), &mode) != FALSE;
}

// Removes SGR sequences (ESC [ ... m).
std::string StripSgr(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[')
        {
            const size_t m = s.find('m', i + 2);
            if (m != std::string::npos)
            {
                i = m;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

void Out(const std::string &utf8)
{
    const std::string text = g_plain ? StripSgr(utf8) : utf8;
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
}

std::string PathText(const fs::path &p)
{
    return util::WideToUtf8(p.wstring());
}

std::string Join(const std::vector<std::string> &items, const char *sep)
{
    std::string out;
    for (const auto &i : items)
        out += (out.empty() ? "" : sep) + i;
    return out;
}

std::string Shorten(std::string s, size_t max)
{
    if (s.size() > max)
        s = s.substr(0, max - 3) + "...";
    return s;
}

} // namespace

int RunInitRules()
{
    const fs::path path = config::HighlightConfigPath();
    std::error_code ec;
    if (fs::exists(path, ec))
    {
        console::PrintError("winssh: " + PathText(path) + " already exists; edit it, or delete it first.\r\n");
        return 1;
    }
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary);
    out << config::StarterConfig();
    if (!out)
    {
        console::PrintError("winssh: cannot write " + PathText(path) + "\r\n");
        return 1;
    }
    Out("Created " + PathText(path) + "\r\nEdit it, then check it with: winssh --check-rules\r\n");
    return 0;
}

int RunCheckRules(const cli::Options &options)
{
    console::ConsoleModeGuard guard;
    guard.EnableVtOutput();
    g_plain = !StdoutIsConsole();

    const fs::path path = config::HighlightConfigPath();
    const auto cfg = config::LoadHighlightConfig(path);
    Out("Rules file: " + PathText(path) +
        (cfg.fileFound ? "\r\n" : " (not found: using built-in rules; create one with winssh --init-rules)\r\n"));

    bool failed = !cfg.errors.empty();
    for (const auto &setName : cfg.ruleSets.order)
    {
        const auto &rules = cfg.ruleSets.sets.at(setName);
        const auto compiled = highlight::CompileRules(rules);
        Out("\r\nRule set \"" + setName + "\" (" + std::to_string(rules.size()) + " rules)\r\n");
        size_t ci = 0; // index into compiled styles (skips disabled and invalid rules)
        for (const auto &rule : rules)
        {
            const bool ok = ci < compiled.names.size() && compiled.names[ci] == rule.name && rule.enabled;
            std::string preview = rule.name;
            if (ok)
                preview = compiled.styles[ci++] + rule.name + "\x1b[0m";
            const std::string what = rule.keywords.empty() ? "regex: " + rule.regex
                                                           : "keywords: " + Join(rule.keywords, ", ");
            Out("  " + preview + std::string(rule.name.size() < 18 ? 18 - rule.name.size() : 1, ' ') +
                Shorten(what, 70) + (rule.enabled ? "" : "  (disabled)") + "\r\n");
        }
        for (const auto &e : compiled.errors)
            Out("  \x1b[31merror:\x1b[0m " + e + "\r\n");
        failed = failed || !compiled.errors.empty();
    }

    for (const auto &e : cfg.errors)
        Out("\r\n\x1b[31merror:\x1b[0m " + e + "\r\n");
    for (const auto &w : cfg.warnings)
        Out("\x1b[33mwarning:\x1b[0m " + w + "\r\n");

    const auto selection = config::SelectRuleSets(cfg.ruleSets, options.highlightSets);
    Out("\r\nSessions use: " + Join(options.highlightSets, ", ") + " (" + std::to_string(selection.rules.size()) +
        " rules)\r\n");
    for (const auto &w : selection.warnings)
    {
        Out("\x1b[31merror:\x1b[0m " + w + "\r\n");
        failed = true;
    }
    Out(failed ? "\r\nResult: problems found (rules with errors are skipped in sessions)\r\n" : "\r\nResult: OK\r\n");
    return failed ? 1 : 0;
}

int RunTestRules(const cli::Options &options)
{
    console::ConsoleModeGuard guard;
    guard.EnableVtOutput();

    RulesSource source(config::HighlightConfigPath(), options.highlightSets);
    auto loaded = source.Load();
    for (const auto &m : loaded.messages)
        console::PrintError("winssh: " + m + "\r\n");
    if (!loaded.compiled.matcher)
    {
        Out(options.testText + "\r\n");
        return loaded.hasErrors ? 1 : 0;
    }
    highlight::Highlighter h(loaded.compiled.matcher, loaded.compiled.styles);
    std::string out = h.Process(options.testText);
    out += h.Flush();
    Out(out + "\r\n");
    return loaded.hasErrors ? 1 : 0;
}

} // namespace winssh::app
