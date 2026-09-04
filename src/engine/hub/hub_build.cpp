// =============================================================================
//  engine/hub/hub_build.cpp  —  HubView assembly (reads through assets::)
// =============================================================================
//  This used to carry its own copy of "read the manifest, validate it, hash every
//  asset" and its own channel reader. Both now come from the one implementation —
//  engine::inspect and engine::current_release — so the hub cannot report a project
//  as shippable that publish would refuse, or vice versa.
// =============================================================================
#include "engine/hub/hub_build.hpp"

#include "engine/assets.hpp"
#include "engine/project/inspect.hpp"
#include "engine/release/ops.hpp"
#include "engine/release/release.hpp"

namespace engine {

std::optional<HubView> build_hub_view(const std::string& path,
                                      const std::vector<std::string>& known_entries) {
    const Inspection in = inspect(path, known_entries);
    // An unreadable or unparseable manifest is not a hub view with problems — there is
    // no project to have a view OF. The caller reports that; everything below assumes
    // a manifest that at least parsed.
    if (!in.parsed) return std::nullopt;

    HubView v;
    v.name   = in.project.name;
    v.entry  = in.project.entry;
    v.schema = in.project.schema;
    v.problems      = in.problems;
    v.shippable     = in.shippable();
    v.local_package = in.package;

    for (const auto& name : well_known_channels()) {
        HubChannel c;
        c.name = name;
        if (auto rel = current_release(name)) {
            c.release       = *rel;
            c.present       = assets::load_file(release_manifest_path(*rel)).has_value();
            c.matches_local = !v.local_package.empty() && *rel == v.local_package;
        }
        v.channels.push_back(c);
    }
    return v;
}

} // namespace engine
