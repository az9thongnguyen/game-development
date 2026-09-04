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

// A subsequence match, folded to lower case. Not a fuzzy score: ordering the results
// by quality would mean the same keystrokes select different commands as the registry
// grows, and a palette whose first entry moves is a palette you have to read.
bool subsequence(std::string_view needle, std::string_view hay) {
    const auto lower = [](char ch) {
        return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
    };
    std::size_t i = 0;
    for (char ch : hay) {
        if (i == needle.size()) break;
        if (lower(ch) == lower(needle[i])) ++i;
    }
    return i == needle.size();
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

std::vector<std::size_t> filter(std::string_view query) {
    std::vector<std::size_t> out;
    const auto& v = infos();
    for (std::size_t i = 0; i < v.size(); ++i) {
        const std::string hay = v[i].id + "  " + v[i].title;
        if (query.empty() || subsequence(query, hay)) out.push_back(i);
    }
    return out;
}

bool unregister(std::string_view id) {
    const int at = index_of(id);
    if (at < 0) return false;
    infos().erase(infos().begin() + at);
    handlers().erase(handlers().begin() + at);
    return true;
}

engine::OpResult run(std::string_view id, const std::vector<std::string>& args) {
    const int at = index_of(id);
    if (at < 0) return engine::OpResult{false, "unknown command: " + std::string(id)};
    return handlers()[static_cast<std::size_t>(at)](args);
}

void clear() { infos().clear(); handlers().clear(); }

} // namespace cmd
