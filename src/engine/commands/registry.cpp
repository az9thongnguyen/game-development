// =============================================================================
//  engine/commands/registry.cpp
// =============================================================================
#include "engine/commands/registry.hpp"

namespace cmd {
namespace {

// Parallel vectors rather than a map of structs: `all()` must hand back a stable,
// registration-ordered list for a palette, and the lookup set is tiny enough that a
// linear scan is not worth a second container to keep in step.
std::vector<Info>&    infos()    { static std::vector<Info> v;    return v; }
std::vector<Handler>& handlers() { static std::vector<Handler> v; return v; }

int index_of(std::string_view id) {
    const auto& v = infos();
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].id == id) return static_cast<int>(i);
    return -1;
}

} // namespace

void register_command(Info info, Handler handler) {
    if (info.id.empty() || !handler) return;
    if (const int at = index_of(info.id); at >= 0) {
        infos()[static_cast<std::size_t>(at)]    = std::move(info);
        handlers()[static_cast<std::size_t>(at)] = std::move(handler);
        return;
    }
    infos().push_back(std::move(info));
    handlers().push_back(std::move(handler));
}

const std::vector<Info>& all() { return infos(); }

bool exists(std::string_view id) { return index_of(id) >= 0; }

engine::OpResult run(std::string_view id, const std::vector<std::string>& args) {
    const int at = index_of(id);
    if (at < 0) return engine::OpResult{false, "unknown command: " + std::string(id)};
    return handlers()[static_cast<std::size_t>(at)](args);
}

void clear() { infos().clear(); handlers().clear(); }

} // namespace cmd
