// M2 spike for D5: std::regex vs PCRE2 (JIT) vs RE2 for keyword highlighting.
// Usage: regex_bench [real-log-file...]
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <re2/re2.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace
{

// WinSerial's built-in rules, one capture group per rule (group N = rule N).
const std::vector<std::string> kRules = {
    R"((?i:\b(?:error|failed|no|down|disabled|unknown|fault|shutdown|disconnected|denied|refused|problem|failure)\b))",
    R"((?i:\b(?:enabled|connected|up|yes|ok|success)\b))",
    R"(\b(?:\d{1,3}\.){3}\d{1,3}\b)",
    R"((?i:\b(?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}\b))",
    R"((?i:\b(?:localhost|vlan)\b))",
    R"((?i:\b(?:ip address|show|interface|route|spanning-tree)\b))",
};

std::string Combined()
{
    std::string p;
    for (const auto &r : kRules)
        p += (p.empty() ? "" : "|") + std::string("(") + r + ")";
    return p;
}

// std::regex has no (?i:...) groups: same rules with icase on the whole pattern.
std::string CombinedStd()
{
    std::string p = Combined();
    for (std::string from : {"(?i:"})
        for (size_t pos; (pos = p.find(from)) != std::string::npos;)
            p.replace(pos, from.size(), "(?:");
    return p;
}

std::vector<std::string> SyntheticLines(size_t targetBytes)
{
    const char *words[] = {"kernel:", "eth0", "link", "is", "up", "down", "fc-transaction", "started", "pump", "1",
                           "pulser", "count", "error", "ok", "connected", "to", "server", "timeout", "retry",
                           "systemd[1]:", "Started", "Failed", "unit", "fc-io.service", "enabled", "value=0",
                           "show", "interface", "vlan", "10", "denied", "user", "root", "session", "opened"};
    std::mt19937 rng(42);
    std::vector<std::string> lines;
    size_t total = 0;
    while (total < targetBytes)
    {
        std::ostringstream l;
        l << "Nov 26 16:4" << rng() % 10 << ":" << 10 + rng() % 50 << " fuelcloud-E13F8 ";
        const int n = 6 + rng() % 12;
        for (int i = 0; i < n; ++i)
        {
            const unsigned k = rng() % 100;
            if (k < 4)
                l << "10.0." << rng() % 256 << "." << rng() % 256 << " ";
            else if (k < 6)
                l << "a4:5e:60:" << std::hex << rng() % 256 << ":" << rng() % 256 << ":" << rng() % 256 << std::dec << " ";
            else
                l << words[rng() % (sizeof(words) / sizeof(words[0]))] << " ";
        }
        l << "\n";
        lines.push_back(l.str());
        total += lines.back().size();
    }
    return lines;
}

struct Result
{
    double ms = 0;
    std::vector<long> perRule;
};

Result RunStd(const std::vector<std::string> &lines)
{
    std::regex re(CombinedStd(), std::regex::icase | std::regex::optimize);
    Result r;
    r.perRule.assign(kRules.size(), 0);
    auto t0 = std::chrono::steady_clock::now();
    for (const auto &line : lines)
        for (std::sregex_iterator it(line.begin(), line.end(), re), end; it != end; ++it)
            for (size_t g = 1; g <= kRules.size(); ++g)
                if ((*it)[g].matched)
                {
                    r.perRule[g - 1]++;
                    break;
                }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

Result RunPcre2(const std::vector<std::string> &lines)
{
    int err = 0;
    PCRE2_SIZE errOff = 0;
    const std::string p = Combined();
    pcre2_code *re = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(p.c_str()), p.size(), PCRE2_UTF, &err, &errOff, nullptr);
    if (!re)
    {
        std::printf("pcre2 compile error %d at %zu\n", err, errOff);
        std::exit(1);
    }
    const int jit = pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
    pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, nullptr);
    Result r;
    r.perRule.assign(kRules.size(), 0);
    auto t0 = std::chrono::steady_clock::now();
    for (const auto &line : lines)
    {
        PCRE2_SIZE pos = 0;
        const auto *s = reinterpret_cast<PCRE2_SPTR>(line.data());
        for (;;)
        {
            const int rc = jit == 0 ? pcre2_jit_match(re, s, line.size(), pos, 0, md, nullptr)
                                    : pcre2_match(re, s, line.size(), pos, 0, md, nullptr);
            if (rc < 0)
                break;
            const PCRE2_SIZE *ov = pcre2_get_ovector_pointer(md);
            for (int g = 1; g < rc; ++g)
                if (ov[2 * g] != PCRE2_UNSET)
                {
                    r.perRule[g - 1]++;
                    break;
                }
            pos = ov[1] > ov[0] ? ov[1] : ov[1] + 1;
            if (pos > line.size())
                break;
        }
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  (pcre2 jit %s)\n", jit == 0 ? "on" : "OFF");
    pcre2_match_data_free(md);
    pcre2_code_free(re);
    return r;
}

Result RunRe2(const std::vector<std::string> &lines)
{
    re2::RE2 re(Combined());
    if (!re.ok())
    {
        std::printf("re2 error: %s\n", re.error().c_str());
        std::exit(1);
    }
    const int ngroups = re.NumberOfCapturingGroups();
    std::vector<re2::StringPiece> sub(static_cast<size_t>(ngroups) + 1);
    Result r;
    r.perRule.assign(kRules.size(), 0);
    auto t0 = std::chrono::steady_clock::now();
    for (const auto &line : lines)
    {
        const re2::StringPiece text(line);
        size_t pos = 0;
        while (pos <= text.size() &&
               re.Match(text, pos, text.size(), re2::RE2::UNANCHORED, sub.data(), ngroups + 1))
        {
            for (int g = 1; g <= ngroups; ++g)
                if (sub[g].data() != nullptr)
                {
                    r.perRule[g - 1]++;
                    break;
                }
            const size_t end = static_cast<size_t>(sub[0].data() - text.data()) + sub[0].size();
            pos = sub[0].empty() ? end + 1 : end;
        }
    }
    r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return r;
}

void Print(const char *name, const Result &r, size_t bytes)
{
    std::printf("%-10s %8.1f ms  %8.1f MB/s  matches:", name, r.ms, bytes / 1e6 / (r.ms / 1e3));
    for (long c : r.perRule)
        std::printf(" %ld", c);
    std::printf("\n");
}

void Pathological()
{
    // Catastrophic backtracking: (a+)+$ on "aaaa...ab"
    const std::string bad = "(a+)+$";
    const std::string subject = std::string(28, 'a') + "b";
    std::printf("\nPathological user rule %s on %zu chars:\n", bad.c_str(), subject.size());

    int err = 0;
    PCRE2_SIZE off = 0;
    pcre2_code *re = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(bad.c_str()), bad.size(), 0, &err, &off, nullptr);
    pcre2_jit_compile(re, PCRE2_JIT_COMPLETE);
    pcre2_match_data *md = pcre2_match_data_create_from_pattern(re, nullptr);
    pcre2_match_context *mc = pcre2_match_context_create(nullptr);
    pcre2_set_match_limit(mc, 100000);
    auto t0 = std::chrono::steady_clock::now();
    const int rc = pcre2_match(re, reinterpret_cast<PCRE2_SPTR>(subject.data()), subject.size(), 0, 0, md, mc);
    std::printf("  pcre2 (match limit 1e5): rc=%d (%s) in %.2f ms\n", rc,
                rc == PCRE2_ERROR_MATCHLIMIT ? "MATCHLIMIT" : "other",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    pcre2_match_context_free(mc);
    pcre2_match_data_free(md);
    pcre2_code_free(re);

    re2::RE2 r2(bad);
    t0 = std::chrono::steady_clock::now();
    const bool m = re2::RE2::PartialMatch(subject, r2);
    std::printf("  re2: match=%d in %.2f ms\n", m,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}

} // namespace

int main(int argc, char **argv)
{
    std::vector<std::string> lines = SyntheticLines(8 * 1000 * 1000);
    for (int i = 1; i < argc; ++i)
    {
        std::ifstream in(argv[i], std::ios::binary);
        std::string l;
        while (std::getline(in, l))
            lines.push_back(l + "\n");
    }
    size_t bytes = 0;
    for (const auto &l : lines)
        bytes += l.size();
    std::printf("corpus: %zu lines, %.1f MB\n", lines.size(), bytes / 1e6);

    Print("PCRE2", RunPcre2(lines), bytes);
    Print("RE2", RunRe2(lines), bytes);

    // std::regex is slow: run on the first 5% and extrapolate the speed.
    std::vector<std::string> part(lines.begin(), lines.begin() + lines.size() / 20);
    size_t partBytes = 0;
    for (const auto &l : part)
        partBytes += l.size();
    Result s = RunStd(part);
    std::printf("std::regex (5%% sample, counts not comparable):\n");
    Print("std", s, partBytes);
    Result p = RunPcre2(part);
    Print("PCRE2-5%", p, partBytes);

    Pathological();
    return 0;
}
