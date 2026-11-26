// Loads the selected highlight rule sets from highlight.json and notices when the file changes.
#pragma once

#include "highlight/Rules.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace winssh::app
{

class RulesSource
{
public:
    RulesSource(std::filesystem::path path, std::vector<std::string> sets);

    struct Loaded
    {
        highlight::CompiledRules compiled;
        std::vector<std::string> messages; // errors and warnings, ready to show
        bool fileFound = false;
        size_t ruleCount = 0;
        bool hasErrors = false;
    };

    // Reads the file, selects the sets, compiles the rules and remembers the file's timestamp.
    Loaded Load();

    // True when the file was created, changed or deleted since the last Load().
    bool Changed() const;

    const std::filesystem::path &Path() const { return path_; }
    const std::vector<std::string> &Sets() const { return sets_; }

private:
    struct Stamp
    {
        bool exists = false;
        std::filesystem::file_time_type time{};
        std::uintmax_t size = 0;
        bool operator==(const Stamp &o) const { return exists == o.exists && time == o.time && size == o.size; }
    };
    Stamp Current() const;

    std::filesystem::path path_;
    std::vector<std::string> sets_;
    Stamp stamp_;
};

} // namespace winssh::app
