// =============================================================================
//  engine/hub/hub.cpp  —  the hub's "next recommended action" decision
// =============================================================================
#include "engine/hub/hub.hpp"

namespace engine {

namespace {
const HubChannel* find(const HubView& v, const std::string& name) {
    for (const auto& c : v.channels)
        if (c.name == name) return &c;
    return nullptr;
}

// Left-pad a channel name to a fixed column so pointers line up in both renderings.
std::string pad(const std::string& s) {
    return s.size() >= 11 ? s : s + std::string(11 - s.size(), ' ');
}
}  // namespace

std::string recommend(const HubView& v) {
    // Not shippable yet: the only useful next step is to fix the first problem.
    if (!v.shippable)
        return "fix: " + (v.problems.empty() ? std::string("project is not shippable")
                                             : v.problems.front());

    // Shippable: walk the promotion pipeline development -> preview -> production and
    // recommend the first step that isn't in sync. "matches_local" catches source that
    // has drifted from what was last published (re-publish before promoting stale bytes).
    const HubChannel* dev  = find(v, "development");
    const HubChannel* prev = find(v, "preview");
    const HubChannel* prod = find(v, "production");

    if (!dev || dev->release.empty() || !dev->matches_local)
        return "publish: your source is not yet the development release";
    if (!prev || prev->release != dev->release)
        return "promote: development -> preview";
    if (!prod || prod->release != prev->release)
        return "promote: preview -> production";
    return "in sync: production matches your source";
}

std::vector<std::string> hub_lines(const HubView& v) {
    std::vector<std::string> out;
    out.push_back("Hub  " + v.name);
    out.push_back("entry " + v.entry + "   schema " + std::to_string(v.schema) +
                  "   " + (v.shippable ? "shippable" : "NOT shippable"));
    for (const auto& p : v.problems) out.push_back("  - " + p);
    if (v.shippable) out.push_back("package " + v.local_package);
    for (const auto& c : v.channels) {
        if (c.release.empty()) { out.push_back(pad(c.name) + " unset"); continue; }
        out.push_back(pad(c.name) + " " + c.release + "  [" +
                      std::string(c.present ? "present" : "MISSING") +
                      (c.matches_local ? ", ==source" : "") + "]");
    }
    out.push_back("next: " + recommend(v));
    return out;
}

} // namespace engine
