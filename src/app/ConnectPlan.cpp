#include "app/ConnectPlan.h"

#include "util/Paths.h"

namespace winssh::app
{

ConnectPlan ResolveConnect(const cli::Options &o, const std::vector<config::Profile> &profiles)
{
    ConnectPlan plan;
    const config::Profile *p = o.destinationMayBeProfile ? config::FindProfile(profiles, o.destination) : nullptr;
    if (p)
    {
        plan.profileId = p->id;
        plan.displayName = p->name.empty() ? p->id : p->name;
        plan.host = p->host;
        plan.port = p->port;
        plan.user = p->user;
        plan.identityFile = p->identityFile;
        if (!p->highlight.empty())
            plan.highlightSets = p->highlight;
        plan.keepAliveSec = p->keepAliveSec;
        plan.hostKeyPolicy = p->hostKeyPolicy;
        plan.legacyAlgorithms = p->legacyAlgorithms;
        plan.savePassword = p->savePassword;
        plan.reconnect = p->autoReconnect;
    }
    else
    {
        plan.host = o.host;
        plan.port = o.port;
        plan.user = o.user;
    }

    // Explicit flags win over profile values.
    if (o.userFlag)
        plan.user = *o.userFlag;
    if (o.portFlag)
        plan.port = *o.portFlag;
    if (!o.identityFile.empty())
        plan.identityFile = o.identityFile;
    if (o.highlightSetsGiven)
        plan.highlightSets = o.highlightSets;
    if (o.keepAliveSec)
        plan.keepAliveSec = *o.keepAliveSec;
    if (o.hostKeyPolicy)
        plan.hostKeyPolicy = config::ParseHostKeyPolicy(*o.hostKeyPolicy).value_or(config::HostKeyPolicy::Strict);
    if (o.legacyAlgorithms)
        plan.legacyAlgorithms = true;
    if (o.reconnect)
        plan.reconnect = *o.reconnect;
    // A dead connection (device rebooted without closing TCP) is only noticed when something is
    // sent; keepalives make that happen. Explicit --keepalive 0 is respected.
    if (plan.reconnect && plan.keepAliveSec == 0 && !o.keepAliveSec)
        plan.keepAliveSec = kReconnectKeepAliveSec;
    plan.highlight = o.highlight;
    plan.identityFile = util::ExpandHome(plan.identityFile);
    if (plan.displayName.empty())
        plan.displayName = (plan.user.empty() ? "" : plan.user + "@") + plan.host;
    return plan;
}

config::Profile ProfileFromOptions(const cli::Options &o, const config::Profile *existing)
{
    config::Profile p = existing ? *existing : config::Profile{};
    p.id = existing ? existing->id : o.profileId;
    if (p.name.empty())
        p.name = p.id;
    p.host = o.host;
    p.port = o.port;
    if (o.userFlag || !o.user.empty() || !existing)
        p.user = o.user;
    if (!o.identityFile.empty())
        p.identityFile = o.identityFile;
    if (o.highlightSetsGiven)
        p.highlight = o.highlightSets;
    if (o.keepAliveSec)
        p.keepAliveSec = *o.keepAliveSec;
    if (o.hostKeyPolicy)
        p.hostKeyPolicy = config::ParseHostKeyPolicy(*o.hostKeyPolicy).value_or(config::HostKeyPolicy::Strict);
    if (o.legacyAlgorithms)
        p.legacyAlgorithms = true;
    if (o.savePassword)
        p.savePassword = true;
    if (o.reconnect)
        p.autoReconnect = *o.reconnect;
    return p;
}

} // namespace winssh::app
