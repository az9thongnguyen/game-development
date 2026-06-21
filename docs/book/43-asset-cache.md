# Chapter 43 — The Asset Cache

> **What this is.** Subsystem **D**, part one: a cache that loads assets through
> registered per-type loaders and hands back **shared, deduplicated** instances. It
> sits on the I/O seam (ch07) and reuses the type-id trick from the ECS (ch39). Hot
> reload is chapter 44. Code: `src/engine/asset_cache.{hpp,cpp}`,
> `src/engine/assets.cpp` (`mtime`).

---

## 1. Why a cache

Loading "player.hrt" from ten places shouldn't read and parse the file ten times, nor
make ten copies in memory. A cache solves both: the first `load` parses and stores the
result; every later `load` of the same asset returns the **same** shared instance. You
get deduplication, shared ownership, and one obvious place to add hot reload (ch44).

```
load<Image>("hero.hrt")  ─┬─ first call:  read bytes → loader → store → return shared_ptr
                          └─ later calls: return the SAME shared_ptr (no re-parse)
```

## 2. Loaders: turning bytes into typed assets

The cache doesn't know how to parse anything — you teach it, per type, with a
**loader**: a function from raw bytes to a `shared_ptr<T>` (null on failure).

```cpp
AssetCache cache;
cache.register_loader<gfx::Image>([](const std::vector<uint8_t>& bytes) {
    auto img = gfx::load_image_from_bytes(bytes);     // the .hrt parser (ch14)
    return img ? std::make_shared<gfx::Image>(std::move(*img)) : nullptr;
});
```

Now `cache.load<gfx::Image>("hero.hrt")` works. Register a different loader for a
different type (a level file, a sound) and the same cache holds them all.

## 3. Type erasure (again): one cache, many types

How does one cache store `shared_ptr<Image>`, `shared_ptr<Level>`, … together? The
same **type-erasure** pattern as the ECS (ch39):

- A per-type id from a static counter, `type_id<T>()`.
- Loaders stored as `type_id → function<shared_ptr<void>(bytes)>` (a `shared_ptr<T>`
  converts to `shared_ptr<void>` while **keeping T's deleter**, so `~T` still runs —
  no leak, no slicing).
- `load<T>` does `static_pointer_cast<T>` on the stored `shared_ptr<void>`.

### The key is (type, path), not just path

A subtle but important point — and a real bug the unit test caught. If entries were
keyed by **path alone**, then after `load<Image>("x")`, a `load<Sound>("x")` would find
the cached entry and `static_pointer_cast<Sound>` an `Image` — type confusion, UB. So
entries are keyed by **`(type_id<T>, path)`**:

```cpp
const std::string key = make_key(type_id<T>(), path);   // e.g. "3:hero.hrt"
```

Now `load<U>` can never alias an entry created by `load<T>` for a different `T`; a
request for a type with no loader simply returns `nullptr`.

## 4. load<T>: the flow

```cpp
template <class T>
std::shared_ptr<T> load(const std::string& path) {
    const std::size_t tid = type_id<T>();
    const std::string key = make_key(tid, path);
    if (auto it = entries_.find(key); it != entries_.end())   // cache hit
        return std::static_pointer_cast<T>(it->second.data);

    auto lit = loaders_.find(tid);
    if (lit == loaders_.end()) return nullptr;                // no loader for T
    auto bytes = load_file(path);                             // via the I/O seam
    if (!bytes) return nullptr;                               // missing file
    std::shared_ptr<void> data = lit->second(*bytes);
    if (!data) return nullptr;                                // parse failure

    Entry e{ path, data, mtime(path), /*reparse closure*/ };  // (ch44)
    entries_.emplace(key, std::move(e));
    return std::static_pointer_cast<T>(data);
}
```

Three failure modes all return a clean `nullptr` — missing file, no registered loader,
parse failure — so callers check the pointer (the engine's house style).

## 5. The `mtime` seam addition

Hot reload needs to know *when a file changed*. That's a new one-line capability on the
I/O seam:

```cpp
std::int64_t assets::mtime(const std::string& path) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(g_base + "/" + path, ec);  // no-throw
    if (ec) return 0;                                  // missing, or web (no FS watch)
    return static_cast<std::int64_t>(t.time_since_epoch().count());
}
```

It returns `0` when the file is missing or the platform can't report a time — which is
exactly the web case, so hot reload simply does nothing there (chapter 44). Like all
I/O, it lives behind the seam so the web build can override it in one place.

## 6. Pitfalls

- **Keying by path only.** Type-confusion waiting to happen — key by `(type, path)`.
- **`operator[]` on the loader map.** It silently inserts an empty `std::function`;
  calling it throws `bad_function_call`. Use `find`.
- **Assuming `load` can't fail.** Missing file / no loader / bad parse → `nullptr`.
- **Forgetting the deleter survives.** It does: `shared_ptr<void>` from `shared_ptr<T>`
  keeps T's deleter. Don't "fix" it with a manual cast.

## 7. Glossary

- **Loader** — a registered function turning bytes into a `shared_ptr<T>`.
- **Cache key** — `(type_id<T>, path)`; makes lookups type-safe.
- **Type erasure** — storing many `shared_ptr<T>` behind `shared_ptr<void>` + a type id.
- **mtime** — file modification time; the hot-reload change signal.

## 8. Exercises

1. **A second loader.** Register a loader for a tiny text "config" type and load one.
2. **Cache stats.** Add hit/miss counters and print the hit rate after a scene loads.
3. **Eviction.** Add `unload(path)` and decide what happens to outstanding
   `shared_ptr`s (hint: they keep the data alive until released).
4. **Bytes vs path loaders.** Some assets reference other files. Sketch how a loader
   could call back into the cache to load sub-assets (and read ch44's reentrancy note).

*(Next: chapter 44 — hot reload.)*
