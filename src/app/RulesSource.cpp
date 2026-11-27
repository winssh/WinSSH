#include "app/RulesSource.h"

#include "config/HighlightConfig.h"

namespace winssh::app
{

RulesSource::RulesSource(std::filesystem::path path, std::vector<std::string> sets)
    : path_(std::move(path)), sets_(std::move(sets))
{
}

RulesSource::Stamp RulesSource::Current() const
{
    Stamp s;
    std::error_code ec;
    s.exists = std::filesystem::is_regular_file(path_, ec);
    if (s.exists)
    {
        s.time = std::filesystem::last_write_time(path_, ec);
        s.size = std::filesystem::file_size(path_, ec);
    }
    return s;
}

bool RulesSource::Changed() const
{
    return !(Current() == stamp_);
}

RulesSource::Loaded RulesSource::Load()
{
    stamp_ = Current();
    Loaded out;
    const auto cfg = config::LoadHighlightConfig(path_);
    out.fileFound = cfg.fileFound;
    for (const auto &e : cfg.errors)
        out.messages.push_back("error: " + e);
    for (const auto &w : cfg.warnings)
        out.messages.push_back("warning: " + w);

    const auto selection = config::SelectRuleSets(cfg.ruleSets, sets_);
    for (const auto &w : selection.warnings)
        out.messages.push_back("warning: " + w);

    out.compiled = highlight::CompileRules(selection.rules);
    for (const auto &e : out.compiled.errors)
        out.messages.push_back("error: " + e);
    out.ruleCount = out.compiled.names.size();
    out.hasErrors = !cfg.errors.empty() || !out.compiled.errors.empty();
    return out;
}

} // namespace winssh::app
