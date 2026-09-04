// =============================================================================
//  engine/project/inspect.cpp
// =============================================================================
#include "engine/project/inspect.hpp"

#include "engine/assets.hpp"

namespace engine {

std::vector<PackagedResource> Inspection::resources() const {
    std::vector<PackagedResource> out;
    out.reserve(assets.size());
    for (const auto& a : assets)
        if (a.present) out.push_back({a.type, a.path, a.hash});
    return out;
}

Inspection inspect(const std::string& project_path,
                   const std::vector<std::string>& known_entries) {
    Inspection in;
    in.path = project_path;

    auto bytes = assets::load_file(project_path);
    if (!bytes) {
        in.problems.push_back("cannot read '" + project_path + "'");
        return in;
    }
    in.readable = true;

    auto parsed = parse_project(std::string(bytes->begin(), bytes->end()));
    if (!parsed) {
        in.problems.push_back("'" + project_path + "' is not a valid gameproject1 manifest");
        return in;
    }
    in.parsed  = true;
    in.project = *parsed;

    // Validation errors first, then unresolved content: a wrong entry id explains a
    // whole broken project, a missing sprite explains one asset. Ordering the list by
    // how much it explains is what makes reading only the first line usually enough.
    for (const auto& e : validate(in.project, known_entries)) in.problems.push_back(e);

    for (const auto& a : in.project.assets) {
        InspectedAsset ia;
        ia.type = a.type;
        ia.path = a.path;
        if (auto ab = assets::load_file(a.path)) {
            ia.present = true;
            ia.bytes   = ab->size();
            ia.hash    = content_hash(std::vector<std::uint8_t>(ab->begin(), ab->end()));
        } else {
            in.problems.push_back("missing asset: " + a.path);
        }
        in.assets.push_back(std::move(ia));
    }

    if (in.shippable()) in.package = hash_hex(package_hash(in.resources()));
    return in;
}

} // namespace engine
