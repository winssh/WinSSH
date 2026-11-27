#include "highlight/Rules.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <charconv>
#include <cstdio>

namespace winssh::highlight
{
namespace
{

// Backtracking limit per match attempt: a bad user regex stops instead of freezing the session.
constexpr uint32_t kMatchLimit = 100000;

struct ColorName
{
    const char *name;
    int code; // 30..37 normal, 90..97 bright (foreground)
};
constexpr ColorName kColors[] = {
    {"black", 30},         {"red", 31},           {"green", 32},          {"yellow", 33},
    {"blue", 34},          {"magenta", 35},       {"cyan", 36},           {"white", 37},
    {"bright-black", 90},  {"bright-red", 91},    {"bright-green", 92},   {"bright-yellow", 93},
    {"bright-blue", 94},   {"bright-magenta", 95}, {"bright-cyan", 96},   {"bright-white", 97},
    {"gray", 90},          {"grey", 90},
};

// Appends SGR parameters for a color; background adds 10 to the code.
bool AppendColor(const std::string &color, bool background, std::string &params, std::string *error)
{
    auto add = [&params](const std::string &p) {
        if (!params.empty())
            params += ';';
        params += p;
    };
    for (const auto &c : kColors)
    {
        if (color == c.name)
        {
            add(std::to_string(c.code + (background ? 10 : 0)));
            return true;
        }
    }
    if (color.size() == 7 && color[0] == '#')
    {
        unsigned rgb = 0;
        auto [p, ec] = std::from_chars(color.data() + 1, color.data() + 7, rgb, 16);
        if (ec == std::errc() && p == color.data() + 7)
        {
            add(std::string(background ? "48" : "38") + ";2;" + std::to_string((rgb >> 16) & 255) + ";" +
                std::to_string((rgb >> 8) & 255) + ";" + std::to_string(rgb & 255));
            return true;
        }
    }
    int index = -1;
    auto [p, ec] = std::from_chars(color.data(), color.data() + color.size(), index);
    if (ec == std::errc() && p == color.data() + color.size() && index >= 0 && index <= 255)
    {
        add(std::string(background ? "48" : "38") + ";5;" + std::to_string(index));
        return true;
    }
    if (error)
        *error = "unknown color '" + color + "' (use a name like red or bright-cyan, #rrggbb, or 0-255)";
    return false;
}

std::string EscapeLiteral(const std::string &s)
{
    std::string out;
    for (char c : s)
    {
        if (std::string_view("\\^$.|?*+()[]{}").find(c) != std::string_view::npos)
            out += '\\';
        out += c;
    }
    return out;
}

std::string PcreError(int code)
{
    PCRE2_UCHAR buf[256];
    pcre2_get_error_message(code, buf, sizeof(buf));
    return reinterpret_cast<const char *>(buf);
}

class PcreMatcher : public Matcher
{
public:
    PcreMatcher(pcre2_code *code, std::vector<uint32_t> groupOfRule)
        : code_(code), groupOfRule_(std::move(groupOfRule))
    {
        jit_ = pcre2_jit_compile(code_, PCRE2_JIT_COMPLETE) == 0;
        data_ = pcre2_match_data_create_from_pattern(code_, nullptr);
        context_ = pcre2_match_context_create(nullptr);
        pcre2_set_match_limit(context_, kMatchLimit);
    }
    ~PcreMatcher() override
    {
        pcre2_match_context_free(context_);
        pcre2_match_data_free(data_);
        pcre2_code_free(code_);
    }

    void FindAll(std::string_view text, std::vector<Match> &out) const override
    {
        const auto *subject = reinterpret_cast<PCRE2_SPTR>(text.data());
        PCRE2_SIZE pos = 0;
        while (pos <= text.size())
        {
            const int rc = jit_ ? pcre2_jit_match(code_, subject, text.size(), pos, 0, data_, context_)
                                : pcre2_match(code_, subject, text.size(), pos, 0, data_, context_);
            if (rc < 0)
                return; // no more matches, or a limit was hit: leave the rest plain
            const PCRE2_SIZE *ov = pcre2_get_ovector_pointer(data_);
            if (ov[1] <= ov[0])
            {
                pos = ov[0] + 1; // empty match: skip a byte, never highlight
                continue;
            }
            for (size_t r = 0; r < groupOfRule_.size(); ++r)
            {
                const uint32_t g = groupOfRule_[r];
                if (static_cast<int>(g) < rc && ov[2 * g] != PCRE2_UNSET)
                {
                    out.push_back({ov[0], ov[1], static_cast<int>(r)});
                    break;
                }
            }
            pos = ov[1];
        }
    }

private:
    pcre2_code *code_;
    std::vector<uint32_t> groupOfRule_; // outer capture group number of each rule
    bool jit_ = false;
    pcre2_match_data *data_ = nullptr;
    pcre2_match_context *context_ = nullptr;
};

constexpr uint32_t kCompileOptions = PCRE2_UTF | PCRE2_MATCH_INVALID_UTF;

} // namespace

std::optional<std::string> ToSgr(const Style &style, std::string *error)
{
    std::string params;
    if (style.bold)
        params += "1";
    if (style.italic)
        params += params.empty() ? "3" : ";3";
    if (style.underline)
        params += params.empty() ? "4" : ";4";
    if (!style.fg.empty() && !AppendColor(style.fg, false, params, error))
        return std::nullopt;
    if (!style.bg.empty() && !AppendColor(style.bg, true, params, error))
        return std::nullopt;
    if (params.empty())
        return std::string();
    return "\x1b[" + params + "m";
}

std::string RulePattern(const Rule &rule)
{
    std::string body;
    if (!rule.keywords.empty())
    {
        body = "\\b(?:";
        for (size_t i = 0; i < rule.keywords.size(); ++i)
            body += (i ? "|" : "") + EscapeLiteral(rule.keywords[i]);
        body += ")\\b";
    }
    else
    {
        body = rule.regex;
    }
    return rule.caseSensitive ? "(?:" + body + ")" : "(?i:" + body + ")";
}

std::vector<Rule> DefaultRules()
{
    std::vector<Rule> rules;
    Rule errors;
    errors.name = "errors";
    errors.keywords = {"error",    "failed",       "no",     "down",    "disabled", "unknown", "fault",
                       "shutdown", "disconnected", "denied", "refused", "problem",  "failure"};
    errors.style.fg = "red";
    rules.push_back(errors);

    Rule ok;
    ok.name = "success";
    ok.keywords = {"enabled", "connected", "up", "yes", "ok", "success"};
    ok.style.fg = "green";
    rules.push_back(ok);

    Rule ipv4;
    ipv4.name = "ipv4";
    ipv4.regex = R"(\b(?:\d{1,3}\.){3}\d{1,3}\b)";
    ipv4.style.fg = "yellow";
    rules.push_back(ipv4);

    Rule mac;
    mac.name = "mac";
    mac.regex = R"(\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b)";
    mac.style.fg = "yellow";
    rules.push_back(mac);

    Rule net;
    net.name = "network";
    net.keywords = {"localhost", "vlan"};
    net.style.fg = "yellow";
    rules.push_back(net);

    Rule config;
    config.name = "config";
    config.keywords = {"ip address", "show", "interface", "route", "spanning-tree"};
    config.style.fg = "cyan";
    rules.push_back(config);
    return rules;
}

CompiledRules CompileRules(const std::vector<Rule> &rules)
{
    CompiledRules out;
    std::string combined;
    std::vector<uint32_t> groupOfRule;
    uint32_t nextGroup = 1;

    for (const Rule &rule : rules)
    {
        if (!rule.enabled)
            continue;
        const std::string label = "rule '" + rule.name + "': ";
        if (rule.keywords.empty() && rule.regex.empty())
        {
            out.errors.push_back(label + "needs keywords or a regex");
            continue;
        }
        std::string styleError;
        const auto sgr = ToSgr(rule.style, &styleError);
        if (!sgr)
        {
            out.errors.push_back(label + styleError);
            continue;
        }

        // Compile alone first: validates the regex and tells how many groups it has.
        const std::string pattern = RulePattern(rule);
        int err = 0;
        PCRE2_SIZE offset = 0;
        pcre2_code *one = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.c_str()), pattern.size(),
                                        kCompileOptions, &err, &offset, nullptr);
        if (!one)
        {
            out.errors.push_back(label + PcreError(err));
            continue;
        }
        uint32_t captures = 0;
        pcre2_pattern_info(one, PCRE2_INFO_CAPTURECOUNT, &captures);
        pcre2_code_free(one);

        combined += (combined.empty() ? "(" : "|(") + pattern + ")";
        groupOfRule.push_back(nextGroup);
        nextGroup += 1 + captures;
        out.styles.push_back(*sgr);
        out.names.push_back(rule.name);
    }

    if (combined.empty())
        return out;
    int err = 0;
    PCRE2_SIZE offset = 0;
    pcre2_code *code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(combined.c_str()), combined.size(),
                                     kCompileOptions, &err, &offset, nullptr);
    if (!code)
    {
        // Should not happen (each part compiled alone), e.g. duplicate named groups across rules.
        out.errors.push_back("combined rules: " + PcreError(err));
        out.styles.clear();
        out.names.clear();
        return out;
    }
    out.matcher = std::make_shared<PcreMatcher>(code, std::move(groupOfRule));
    return out;
}

} // namespace winssh::highlight
