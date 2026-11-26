#include "config/SshConfigImport.h"

#include "util/Paths.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <set>

namespace winssh::config
{
namespace
{

std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Splits arguments on whitespace, honouring double quotes.
std::vector<std::string> SplitArgs(std::string_view s)
{
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size())
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        if (i >= s.size())
            break;
        std::string arg;
        if (s[i] == '"')
        {
            ++i;
            while (i < s.size() && s[i] != '"')
                arg += s[i++];
            ++i; // closing quote
        }
        else
        {
            while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])))
                arg += s[i++];
        }
        out.push_back(arg);
    }
    return out;
}

struct Block
{
    std::vector<std::string> patterns; // empty = global section before the first Host
    std::vector<std::pair<std::string, std::string>> options; // keyword (lower case), value
};

bool BlockApplies(const Block &b, const std::string &alias)
{
    if (b.patterns.empty())
        return true;
    bool matched = false;
    for (const auto &p : b.patterns)
    {
        if (!p.empty() && p[0] == '!')
        {
            if (SshPatternMatch(std::string_view(p).substr(1), alias))
                return false; // negated match excludes the block
        }
        else if (SshPatternMatch(p, alias))
        {
            matched = true;
        }
    }
    return matched;
}

bool HasWildcard(const std::string &p)
{
    return p.find_first_of("*?!") != std::string::npos;
}

} // namespace

bool SshPatternMatch(std::string_view pattern, std::string_view text)
{
    // Iterative glob with backtracking on the last '*'.
    size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
    auto eq = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    while (t < text.size())
    {
        if (p < pattern.size() && (pattern[p] == '?' || eq(pattern[p], text[t])))
        {
            ++p;
            ++t;
        }
        else if (p < pattern.size() && pattern[p] == '*')
        {
            star = p++;
            mark = t;
        }
        else if (star != std::string_view::npos)
        {
            p = star + 1;
            t = ++mark;
        }
        else
        {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*')
        ++p;
    return p == pattern.size();
}

SshConfigImport ParseSshConfig(std::string_view text)
{
    SshConfigImport result;
    std::vector<Block> blocks(1); // blocks[0] = global section
    std::vector<std::string> aliases;
    bool skipping = false; // inside a Match block
    bool warnedMatch = false, warnedInclude = false;

    size_t lineNo = 0;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        std::string line(text.substr(pos, end - pos));
        pos = end + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
            continue;
        line = line.substr(first);

        // keyword, then whitespace or '=' (optionally surrounded by whitespace)
        const auto kwEnd = line.find_first_of(" \t=");
        std::string keyword = Lower(line.substr(0, kwEnd));
        std::string rest = kwEnd == std::string::npos ? "" : line.substr(kwEnd);
        const auto valStart = rest.find_first_not_of(" \t=");
        rest = valStart == std::string::npos ? "" : rest.substr(valStart);
        // strip trailing comment "value # note"
        if (const auto hash = rest.find(" #"); hash != std::string::npos)
            rest.erase(hash);
        const auto args = SplitArgs(rest);

        if (keyword == "host")
        {
            skipping = false;
            Block b;
            b.patterns = args;
            blocks.push_back(std::move(b));
            for (const auto &a : args)
                if (!HasWildcard(a) && std::find(aliases.begin(), aliases.end(), a) == aliases.end())
                    aliases.push_back(a);
            continue;
        }
        if (keyword == "match")
        {
            skipping = true;
            if (!warnedMatch)
                result.warnings.push_back("line " + std::to_string(lineNo) + ": Match blocks are not supported (skipped)");
            warnedMatch = true;
            continue;
        }
        if (keyword == "include")
        {
            if (!warnedInclude)
                result.warnings.push_back("line " + std::to_string(lineNo) +
                                          ": Include is not supported (included files are not imported)");
            warnedInclude = true;
            continue;
        }
        if (skipping || args.empty())
            continue;
        blocks.back().options.emplace_back(keyword, args[0]);
    }

    std::set<std::string> ids;
    for (const auto &alias : aliases)
    {
        if (!IsValidProfileId(alias))
        {
            result.warnings.push_back("Host '" + alias + "': not a valid profile id (skipped)");
            continue;
        }
        if (!ids.insert(Lower(alias)).second)
            continue;

        // First value wins across all blocks that apply, in file order (OpenSSH semantics).
        std::map<std::string, std::string> values;
        for (const Block &b : blocks)
        {
            if (!BlockApplies(b, alias))
                continue;
            for (const auto &[k, v] : b.options)
                values.emplace(k, v);
        }

        Profile p;
        p.id = alias;
        p.name = alias;
        p.host = alias;
        if (auto it = values.find("hostname"); it != values.end())
        {
            std::string h = it->second;
            for (size_t i; (i = h.find("%h")) != std::string::npos;)
                h.replace(i, 2, alias);
            p.host = h;
        }
        if (auto it = values.find("user"); it != values.end())
            p.user = it->second;
        if (auto it = values.find("identityfile"); it != values.end() && Lower(it->second) != "none")
            p.identityFile = it->second;
        if (auto it = values.find("port"); it != values.end())
        {
            int port = 0;
            const auto &s = it->second;
            auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), port);
            if (ec == std::errc() && ptr == s.data() + s.size() && port >= 1 && port <= 65535)
                p.port = port;
            else
                result.warnings.push_back("Host '" + alias + "': invalid Port '" + s + "' (using 22)");
        }
        result.profiles.push_back(std::move(p));
    }
    return result;
}

std::filesystem::path DefaultSshConfigPath()
{
    return util::SshDir() / L"config";
}

} // namespace winssh::config
