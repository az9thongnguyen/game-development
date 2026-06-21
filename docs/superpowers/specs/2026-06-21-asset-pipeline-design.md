# Subsystem D — Asset Pipeline + Hot Reload — Design Spec

> Date: 2026-06-21 · Program A→F, step D · Branch `feat/assets`
> Builds on the existing `assets` seam (ch07) + the `.hrt` image loader (ch14).
> Editor (F) depends on this, so it comes before E/F in the build but after C.

## 1. Goal

A small **asset manager** on top of the I/O seam that:
- **Loads** assets through registered, per-type loaders (e.g. `.hrt` → `gfx::Image`).
- **Caches** by path so repeated `load()`s return the same instance (no re-parsing,
  shared ownership).
- **Hot-reloads**: when a file changes on disk, re-parse it and swap the new data into
  the *existing* object, so everything already holding it updates live. Native only;
  the web has no filesystem watch (documented, no-op).

## 2. Decisions (with alternatives)

- **Hot-reload model: in-place content swap** (`*existing = std::move(fresh)`), so
  holders keep their `shared_ptr<T>` and see the new data automatically. Alternative —
  pointer swap in the cache (holders must re-`load()`); rejected because "it just
  updates" is the whole point of hot reload.
- **Type erasure**: a per-type loader (`type_id<T>` from B's pattern) returning
  `shared_ptr<void>`; each cache entry stores a type-erased `reload` closure built at
  `load<T>` time (so it knows `T` for the in-place assignment).
- **Change detection: file mtime polling** via a new `assets::mtime(path)`. Simple,
  portable (std::filesystem), web-safe (returns 0 → no reload). Alternative — OS file
  watchers (inotify/FSEvents): faster but platform-specific and heavier; an exercise.
- **Allocator adoption (A)**: documented as optional (an Arena per load for transient
  parse buffers); not forced — `shared_ptr<T>` ownership is the clean default here.

## 3. Files

```
src/engine/assets.{hpp,cpp}       + mtime(path)  (the change-detection primitive)
src/engine/asset_cache.{hpp,cpp}  AssetCache: register_loader<T>, load<T>, reload(path),
                                  reload_changed(); type-erased entries with in-place reload
tests/test_assets.cpp             CTest 'assets'
docs/book/43,44 (split)
```

CMake: `test_assets` compiles `assets.cpp` + `asset_cache.cpp` directly (matches the
existing test_iso pattern); `demo` adds `asset_cache.cpp`. No SDL.

## 4. API

```cpp
class AssetCache {
public:
    // Register how to turn raw bytes into a shared T (nullptr on parse failure).
    template<class T> void register_loader(std::function<std::shared_ptr<T>(const std::vector<uint8_t>&)> fn);

    // Load (or return cached) the asset at `path`. nullptr if missing / no loader / parse fail.
    template<class T> std::shared_ptr<T> load(const std::string& path);

    bool reload(const std::string& path);   // force re-parse + in-place swap (testable)
    int  reload_changed();                   // re-parse every entry whose mtime changed; returns count
    void clear();
    std::size_t size() const;
};
```

`reload_changed()` is the per-frame hook; `reload(path)` is a force path (and what the
tests use, to avoid depending on mtime timing).

## 5. Hot-reload mechanics

- `load<T>(path)`: cache hit → `static_pointer_cast<T>`; else read bytes
  (`assets::load_file`), run the registered T loader, store an `Entry { data, mtime,
  reload-closure }`, return the `shared_ptr<T>`.
- The entry's `reload` closure (built where `T` is known) re-runs the loader on fresh
  bytes and does `*static_pointer_cast<T>(data) = std::move(*fresh)` — the in-place
  swap. So every holder of the `shared_ptr<T>` sees the update.
- `reload_changed()`: for each entry, if `assets::mtime(path)` differs from the stored
  one, re-read + run the closure; on success bump the stored mtime; on failure bump it
  anyway (don't retry a broken file every frame).

## 6. Correctness focus
- Cache identity: two `load<T>(path)` return the same pointer.
- In-place reload: a held `shared_ptr<T>` reflects new file contents after `reload`.
- Missing file / unregistered type / parse failure → `nullptr`, no crash.
- mtime: >0 for an existing file, 0 for missing; `reload_changed` no-ops when unchanged.
- Type-erased deletion via `shared_ptr<void>` with the correct deleter (shared_ptr
  type-erases the deleter, so `shared_ptr<void>` from `shared_ptr<T>` destroys T
  correctly — verified).

## 7. Tests (`tests/test_assets.cpp`)
register a toy loader (bytes→a struct) + the real `.hrt`→Image path; load caching
(same ptr); force `reload(path)` swaps content in a held ptr; `reload_changed` after a
rewrite; missing/unregistered/parse-fail → nullptr; `assets::mtime` present/absent.
Uses a temp base path + `assets::write_file` to create/modify files. ASan+UBSan.

## 8. Guidebook (split)
- **43 — The asset cache**: loaders, caching by path, shared ownership, `type_id`
  reuse, the `mtime` seam addition.
- **44 — Hot reload**: mtime polling, the in-place content swap, the web caveat, and
  D acceptance + the adoption note (Arena for transient parse buffers).

## 9. Risks
- mtime resolution/timing → tests use the force `reload()` path for determinism; the
  mtime poll is exercised separately.
- shared_ptr<void> deleter correctness → relies on shared_ptr type-erasing the deleter
  (standard, documented + tested).
- Web has no file watch → `mtime` returns 0, `reload_changed` no-ops (documented).
