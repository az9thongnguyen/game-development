// =============================================================================
//  engine/hub/hub_build.cpp  —  HubView assembly (reads through assets::)
// =============================================================================
#include "engine/hub/hub_build.hpp"

#include "engine/assets.hpp"
#include "engine/project/project.hpp"
#include "engine/release/release.hpp"
#include "engine/resource/resource.hpp"

namespace engine {

namespace {
// The channels the hub reports, in pipeline order. Mirrors main.cpp's kChannels;
// duplicated here (three literals) rather than coupling the lib to the CLI.
const char* const kHubChannels[] = {"development", "preview", "production"};

std::optional<std::string> read_channel(const std::string& channel) {
    auto bytes = assets::load_file(channel_path(channel));
    if (!bytes) return std::nullopt;
    return parse_channel(std::string(bytes->begin(), bytes->end()));
}
}  // namespace

std::optional<HubView> build_hub_view(const std::string& path,
                                      const std::vector<std::string>& known_entries) {
    auto bytes = assets::load_file(path);
    if (!bytes) return std::nullopt;
    auto proj = parse_project(std::string(bytes->begin(), bytes->end()));
    if (!proj) return std::nullopt;

    HubView v;
    v.name = proj->name; v.entry = proj->entry; v.schema = proj->schema;

    const auto errs = validate(*proj, known_entries);
    std::vector<PackagedResource> resources;
    std::vector<std::string> missing;
    for (const auto& a : proj->assets) {
        if (auto ab = assets::load_file(a.path))
            resources.push_back({a.type, a.path,
                                 content_hash(std::vector<uint8_t>(ab->begin(), ab->end()))});
        else
            missing.push_back(a.path);
    }
    for (const auto& e : errs)    v.problems.push_back(e);                       // validation errors first,
    for (const auto& m : missing) v.problems.push_back("missing asset: " + m);   // then unresolved content
    v.shippable = v.problems.empty();
    if (v.shippable) v.local_package = hash_hex(package_hash(resources));

    for (const char* ch : kHubChannels) {
        HubChannel c;
        c.name = ch;
        if (auto rel = read_channel(ch)) {
            c.release       = *rel;
            c.present       = assets::load_file(release_manifest_path(*rel)).has_value();
            c.matches_local = !v.local_package.empty() && *rel == v.local_package;
        }
        v.channels.push_back(c);
    }
    return v;
}

} // namespace engine
