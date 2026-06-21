// =============================================================================
//  engine/asset_cache.cpp  —  AssetCache hot-reload (non-template parts)
// =============================================================================
//  reload()/reload_changed() SNAPSHOT what to reparse before running any user loader,
//  because a loader may re-enter load<>() and rehash entries_ (invalidating iterators).
//  Captured copies (path, the shared_ptr, the reparse closure) are independent of the
//  map, so the reparse is safe; the map is re-found only to update mtime afterward.
// =============================================================================
#include "engine/asset_cache.hpp"

namespace assets {

namespace {
struct Pending {
    std::string                                                              key;
    std::string                                                              path;
    std::int64_t                                                             mtime = 0;
    std::shared_ptr<void>                                                    data;
    std::function<bool(std::shared_ptr<void>, const std::vector<uint8_t>&)>  reparse;
};
} // namespace

bool AssetCache::reload(const std::string& path) {
    auto bytes = load_file(path);
    if (!bytes) return false;

    std::vector<Pending> todo;                          // snapshot matching entries
    for (auto& [k, e] : entries_)
        if (e.path == path) todo.push_back({k, e.path, 0, e.data, e.reparse});
    if (todo.empty()) return false;

    bool all_ok = true;
    for (auto& p : todo)
        if (!p.reparse(p.data, *bytes)) all_ok = false;

    const std::int64_t m = mtime(path);                 // re-find to update mtime safely
    for (auto& p : todo)
        if (auto it = entries_.find(p.key); it != entries_.end()) it->second.mtime = m;
    return all_ok;
}

int AssetCache::reload_changed() {
    std::vector<Pending> todo;                           // snapshot entries whose mtime moved
    for (auto& [k, e] : entries_) {
        const std::int64_t m = mtime(e.path);
        if (m == 0 || m == e.mtime) continue;           // missing or unchanged
        todo.push_back({k, e.path, m, e.data, e.reparse});
    }

    int n = 0;
    for (auto& p : todo) {
        auto bytes = load_file(p.path);
        if (!bytes) continue;                           // I/O failure: retry next poll (no mtime bump)
        const bool ok = p.reparse(p.data, *bytes);
        if (ok) ++n;
        // Advance mtime on success OR parse-failure (don't re-spam a broken file);
        // re-find because reparse may have rehashed entries_.
        if (auto it = entries_.find(p.key); it != entries_.end()) it->second.mtime = p.mtime;
    }
    return n;
}

} // namespace assets
