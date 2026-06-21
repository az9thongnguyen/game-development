# Chapter 44 — Hot Reload

> **What this is.** Subsystem **D**, part two: editing an asset on disk and seeing the
> game update **without restarting**. The trick is an **in-place content swap** so
> everything already holding the asset sees the new data. We also handle the
> reentrancy and web caveats, and close D. Code: `src/engine/asset_cache.{hpp,cpp}`.

---

## 1. What hot reload should feel like

You hold a `shared_ptr<Image>` for the hero sprite. You repaint `hero.hrt` in an
editor and save. Next frame, the hero looks different — and you never touched your
pointer. That "it just updates" experience is the design target, and it dictates the
mechanism.

## 2. Two ways to reload (and why we swap in place)

- **Pointer swap:** the cache replaces its stored `shared_ptr` with a freshly loaded
  one. Problem: everyone who already called `load()` still holds the *old* pointer and
  sees stale data unless they re-`load()` every frame.
- **In-place content swap (ours):** load the new asset, then assign its contents into
  the *existing* object: `*existing = std::move(fresh)`. Same object, new data — every
  holder updates automatically.

```cpp
// the reparse closure, built in load<T> where T is known:
std::shared_ptr<void> fresh = loader(new_bytes);
*static_pointer_cast<T>(data) = std::move(*static_pointer_cast<T>(fresh));   // in place
```

This requires `T` to be move/copy-assignable (a hard compile error otherwise — a fair
constraint for an asset). The closure is type-erased into the cache entry, so the cache
can trigger it without knowing `T`.

## 3. Detecting changes: mtime polling

Once per frame (or on demand) the cache asks the OS whether each cached file's
modification time changed, via `assets::mtime` (ch43). If it did, re-read and reparse:

```cpp
int AssetCache::reload_changed() {
    /* snapshot entries whose mtime moved (see §4) */
    for (auto& p : todo) {
        auto bytes = load_file(p.path);
        if (!bytes) continue;                  // I/O hiccup: retry next poll (no mtime bump)
        if (p.reparse(p.data, *bytes)) ++n;    // success → counted
        if (auto it = entries_.find(p.key); it != entries_.end())
            it->second.mtime = p.mtime;        // advance (success or broken parse) so we
    }                                          // don't re-spam a broken file every frame
    // (find, not entries_[p.key]: operator[] would insert an empty entry on a miss)
    return n;
}
```

`reload(path)` is the **force** variant (reparse regardless of mtime) — handy for tools
and for tests, which use it to verify the swap deterministically without depending on
filesystem timestamp resolution.

mtime polling is simple and portable. OS file-change notifications (inotify/FSEvents)
are faster and event-driven but platform-specific — a worthwhile exercise, not needed
for a dev-time feature.

## 4. The reentrancy trap (a subtle one)

A loader might load *sub-assets* — i.e. call back into `cache.load<>()` while the cache
is mid-reload. That `load` does `entries_.emplace`, which can **rehash** the map and
**invalidate iterators**. If `reload_changed()` were iterating `entries_` directly and
holding a reference into it across the user loader, that's undefined behavior.

Two defenses, both applied:

1. **Snapshot first.** `reload`/`reload_changed` copy what they need (key, path, the
   `shared_ptr`, the reparse closure) into a local vector *before* running any loader,
   then iterate the snapshot — never the live map.
2. **Pass the data `shared_ptr` by value.** The reparse closure takes
   `std::shared_ptr<void>` *by value* (a copy). The copy points at the same object, so
   `*copy = …` still updates everyone — but the copy is independent of the map, so a
   rehash mid-reparse can't dangle it.

(Both were tightened after a code review; the naive "iterate the map and pass a
reference" version was UB under reentrant loads.)

## 5. The web caveat

The browser has no filesystem to watch. `assets::mtime` returns `0` there, so
`reload_changed()` simply finds nothing changed and does nothing. Hot reload is a
desktop dev-time convenience; the WASM build is unaffected and still loads assets
through the same cache. No special-casing in game code — the seam absorbs it.

## 6. D acceptance

- [x] **Cache + dedup**: repeated `load<T>(path)` return the same instance.
- [x] **Per-type loaders**, type-safe keys `(type, path)`, clean `nullptr` on
      missing/no-loader/parse-fail.
- [x] **In-place hot reload**: a held `shared_ptr<T>` reflects new file contents after
      `reload` (verified by writing a new file and re-reading through the held pointer).
- [x] **Reentrancy-safe** reload (snapshot + by-value `shared_ptr`); **mtime** seam;
      web no-op.
- [x] **Tests + safety**: `ctest assets` green; ASan+UBSan clean; warning-clean; no SDL.

Verified by `tests/test_assets.cpp` (cache identity, in-place reload content swap,
mtime present/absent, `reload_changed` no-op when unchanged, and the missing /
unregistered-type / parse-failure paths).

## 7. Adoption note (subsystem A)

A loader can use an `mem::Arena` as a scratch buffer for transient parse state, freeing
it all at once when parsing finishes — the asset-pipeline adoption promised in ch37.
The asset's final storage stays a `shared_ptr<T>` (shared ownership across the engine).

## 8. Glossary

- **Hot reload** — updating an asset at runtime from a changed file, no restart.
- **In-place content swap** — `*existing = std::move(fresh)`; updates all holders.
- **Reentrancy** — a loader calling back into the cache mid-reload; handled by
  snapshotting + by-value `shared_ptr`.
- **mtime polling** — detecting change by comparing file modification times.

## 9. Exercises

1. **Live tweak.** Wire `reload_changed()` into a scene's frame loop, run it, edit an
   `.hrt`, and watch the sprite change without restarting.
2. **Reload callbacks.** Add an `on_reload(path, fn)` hook so systems can react (e.g.
   re-upload a texture). 
3. **Sub-asset loader.** Write a loader that loads referenced files via the cache and
   confirm the snapshot/by-value defenses make it safe.
4. **Event-driven.** Replace mtime polling with FSEvents (macOS) or inotify (Linux)
   behind the seam; keep the web no-op.

*(Subsystem D complete. Next: E — the physics engine.)*
