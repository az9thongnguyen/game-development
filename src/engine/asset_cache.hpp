// =============================================================================
//  engine/asset_cache.hpp  —  cache + hot-reload on top of the I/O seam
// =============================================================================
//  Loads assets through registered per-type loaders, caches them by path (repeated
//  loads return the SAME shared instance), and hot-reloads on disk change by parsing
//  the new bytes and swapping them INTO the existing object — so everything already
//  holding the shared_ptr sees the update, no re-fetch. Native only; the web has no
//  filesystem watch (mtime() returns 0 → reload_changed() is a no-op there).
//
//  Loaders + entries are type-erased (a per-type id, like the ECS) so one cache holds
//  any asset type. The in-place reload closure is built where T is known (in load<T>).
// =============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/assets.hpp"

namespace assets {

class AssetCache {
public:
    // Register how to turn raw bytes into a shared T (return nullptr on parse failure).
    template <class T>
    void register_loader(std::function<std::shared_ptr<T>(const std::vector<uint8_t>&)> fn) {
        loaders_[type_id<T>()] =
            [fn = std::move(fn)](const std::vector<uint8_t>& b) -> std::shared_ptr<void> {
                return fn(b);   // shared_ptr<T> → shared_ptr<void> keeps T's deleter
            };
    }

    // Load (or return cached) the asset at `path`. nullptr if missing / no loader for
    // T / parse failure. Repeated calls return the same instance.
    template <class T>
    std::shared_ptr<T> load(const std::string& path) {
        // Key by (type, path): the same file may be loaded as different types, and a
        // load<U> must never alias an entry stored as some other type T.
        const std::size_t tid = type_id<T>();
        const std::string key = make_key(tid, path);
        if (auto it = entries_.find(key); it != entries_.end())
            return std::static_pointer_cast<T>(it->second.data);

        auto lit = loaders_.find(tid);
        if (lit == loaders_.end()) return nullptr;       // no loader registered for T
        auto bytes = load_file(path);
        if (!bytes) return nullptr;
        std::shared_ptr<void> data = lit->second(*bytes);
        if (!data) return nullptr;                       // parse failure

        Entry e;
        e.path  = path;
        e.data  = data;
        e.mtime = mtime(path);
        // `d` is taken BY VALUE (a shared_ptr copy): it points at the same T object, so
        // `*d = …` still updates everything holding it — and the copy is independent of
        // the entries_ map, so a loader that re-enters load<>() (rehashing the map)
        // cannot dangle it.
        e.reparse = [this, tid](std::shared_ptr<void> d, const std::vector<uint8_t>& b) -> bool {
            auto lit2 = loaders_.find(tid);                  // find (not []): don't insert
            if (lit2 == loaders_.end() || !lit2->second) return false;
            std::shared_ptr<void> fresh = lit2->second(b);
            if (!fresh) return false;
            // In-place content swap: holders of the shared_ptr<T> see the new data.
            *std::static_pointer_cast<T>(d) = std::move(*std::static_pointer_cast<T>(fresh));
            return true;
        };
        entries_.emplace(key, std::move(e));
        return std::static_pointer_cast<T>(data);
    }

    bool        reload(const std::string& path);   // force re-parse + in-place swap
    int         reload_changed();                  // re-parse entries whose mtime changed
    void        clear() { entries_.clear(); }
    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::string           path;          // the file (the map key is type+path)
        std::shared_ptr<void> data;
        std::int64_t          mtime = 0;
        std::function<bool(std::shared_ptr<void>, const std::vector<uint8_t>&)> reparse;
    };

    static std::string make_key(std::size_t tid, const std::string& path) {
        return std::to_string(tid) + ":" + path;
    }
    static std::size_t next_type_id() {
        static std::atomic<std::size_t> c{0};   // atomic: safe even across static init
        return c.fetch_add(1);
    }
    template <class T>
    static std::size_t type_id() { static const std::size_t id = next_type_id(); return id; }

    std::unordered_map<std::size_t,
        std::function<std::shared_ptr<void>(const std::vector<uint8_t>&)>> loaders_;
    std::unordered_map<std::string, Entry> entries_;   // key = make_key(type, path)
};

} // namespace assets
