# Chapter 50 — Engine-Core Integration: the Colony Sim

> **What this is.** The payoff chapter. Subsystems A–F were each built, tested, and
> documented *in isolation* — but a library only earns its keep when something *uses*
> it. `--colony` is a small isometric agent sandbox written **entirely on the engine
> core**: it composes **A** (frame allocator), **B** (ECS), **C** (jobs), **D** (asset
> cache + hot reload), and **F** (immediate-mode GUI) into one game. This is how the
> pieces fit together. Code: `src/games/colony/`.

---

## 1. Why this chapter exists

After building A–F, an audit asked a blunt question: *which games actually use them?*
The answer was uncomfortable — **A, B, C, D had zero game users**; E and F only the
editor demo. The systems were correct and documented, but unproven in anger. The
colony sim closes that gap: every line of it leans on engine-core code, so the
abstractions get exercised by a real, interactive program.

It also reuses the **isometric projection** (`engine/iso.hpp`, ch26) and the **A\***
planner + tile map (`iso::astar`/`iso::TileMap`, ch27/29) — generic pieces that didn't
need rebuilding. (A\* arguably belongs in engine-core proper; promoting it is an
exercise.)

## 2. The shape: a pure Sim + a render Scene

The same model/render split as every scene:

```
Sim   (colony.{hpp,cpp})   ── pure, testable, no SDL/renderer ──
   ecs::Registry reg_       (B) entities + components
   iso::TileMap  map_           the floor (dense grid)
   jobs::JobSystem jobs_    (C) parallel agent movement
   spawn_agent/prop · set_goal (A*) · update(dt) [parallel_for]

ColonyScene (colony_scene.{hpp,cpp}) ── render + input ──
   mem::FrameAllocator frame_ (A) per-frame drawable scratch
   assets::AssetCache  cache_ (D) the agent sprite + hot reload
   ui::Context         ui_    (F) the control panel
```

The Sim is unit-tested headless (B + C); the Scene is verified by an offline render.

## 3. B — entities & components (not a fat struct)

Unlike the M4 farm (which hard-coded its pools, ch28), the colony's dynamic things are
generic ECS entities:

```cpp
struct GridPos { float x, y; };                    // where (grid coords, fractional)
struct Visual  { gfx::Color color; bool is_agent; }; // how to draw
struct Agent   { std::vector<iso::Vec2i> path; std::size_t idx; float speed; };

ecs::Entity Sim::spawn_agent(int x, int y, gfx::Color c) {
    ecs::Entity e = reg_.create();
    reg_.add<GridPos>(e, {(float)x, (float)y});
    reg_.add<Visual>(e, {c, true});
    reg_.add<Agent>(e, {});
    return e;
}
```

A prop is the same minus the `Agent` component. Rendering, pathing, and movement all
iterate via `reg_.view<...>()` — add a new behavior by adding a component + a system,
touching nothing else.

## 4. C — parallel movement (and why it's race-free)

`update` advances every agent. Each agent's step is independent, so it's a textbook
`parallel_for`:

```cpp
std::vector<ecs::Entity> ents;
reg_.view<Agent, GridPos>([&](ecs::Entity e, Agent&, GridPos&) { ents.push_back(e); });
jobs_.parallel_for(ents.size(), [&](std::size_t i) {
    Agent*   a = reg_.get<Agent>(ents[i]);
    GridPos* p = reg_.get<GridPos>(ents[i]);
    /* glide p toward a->path[a->idx] at a->speed … */
}, 64);
```

**Is this safe?** Yes, and it's worth understanding precisely:

- Each task writes only **its own** entity's `Agent`/`GridPos` — i.e. *distinct slots*
  in the dense component arrays. Concurrent writes to different elements of the same
  container are data-race-free (no reallocation happens).
- No task **adds or removes** entities during the loop, so the pools (`pools_`,
  `sparse_`) are only *read* concurrently — also safe.
- `reg_.get<T>` touches `type_id<T>()` (a function-local static), but those are already
  initialised on the main thread before the loop, so there's no concurrent
  first-init race.

This is verified, not just argued: a dedicated **ThreadSanitizer** build of
`test_colony` is clean, and a test asserts the parallel result is *identical* to the
synchronous (`workers=0`) one. On the web, `parallel_for` runs synchronously (ch42), so
the colony works there too with no change.

## 5. A — per-frame scratch for the depth sort

Drawing isometrically needs a back-to-front sort each frame (ch27). That sorted list is
pure per-frame scratch — exactly a `FrameAllocator`'s job:

```cpp
frame_.flip();                                   // reclaim last frame's scratch
std::size_t count = /* count Visual+GridPos entities */;
Drawable* draw = frame_.alloc<Drawable>(count);  // A: frame memory, no malloc churn
/* fill from a view, std::sort by depth_key, then draw */
```

No `new`/`delete` per frame, and the memory is reclaimed by `flip()` next frame — the
allocator adoption promised back in ch37.

## 6. D — a cached, hot-reloadable sprite

Agents are drawn with a real sprite loaded through the asset cache. To avoid bundling a
file, the scene **generates** a tiny `.hrt` blob on first run, then loads it via the
cache and polls for changes every frame:

```cpp
if (!assets::load_file(kSpritePath)) assets::write_file(kSpritePath, gen_agent_sprite());
cache_.register_loader<gfx::Image>([](const std::vector<uint8_t>& b) {
    auto img = gfx::decode_hrt(b);               // pure bytes→Image (split from load_image)
    return img ? std::make_shared<gfx::Image>(std::move(*img)) : nullptr;
});
sprite_ = cache_.load<gfx::Image>(kSpritePath);
// each frame:
cache_.reload_changed();                         // edit the .hrt → agents update live
```

(The `decode_hrt`/`load_image` split, ch43, is what lets the cache's bytes-based loader
reuse the engine's `.hrt` parser.)

## 7. F — the control panel

An immediate-mode panel drives it all — no retained widgets, state in the scene:

```cpp
ui_.begin(&g, adapt(ctx.input));
ui_.panel({12,12,200,188}, "COLONY");
ui_.label("agents: N  fps: M");
if (ui_.button("Spawn agent"))   sim_.spawn_agent(...);
if (ui_.button("Send to corner")) sim_.set_goal(...);
ui_.checkbox("running", running_);
ui_.slider("speed", speed_, 0.5f, 8.0f);
if (ui_.button("Reset")) sim_.reset_default();
ui_.end();
if (ctx.input.pressed(Left) && !ui_.hovering_ui()) sim_.set_goal(hovered_);  // click → goal
```

## 8. Run & observe

```sh
cmake --build build && ./build/demo --colony
```

Agents stroll the iso map. Click anywhere to send the whole colony there — they A\*
around the pond, fanning out along their paths. Drag the **speed** slider and watch them
speed up; untick **running** to pause; **Reset** rebuilds the scene. Every one of those
interactions is the engine core doing its job.

## 9. Pitfalls (integration-specific)

- **Structural edits during a parallel loop.** Adding/removing entities mid-`parallel_for`
  would reallocate pools and race. Keep structural changes on the main thread, between
  frames. (The colony only *reads* structure in parallel.)
- **Count-then-fill drift.** The two-pass drawable build assumes no spawn/destroy
  between the passes; a `k < count` guard makes a mismatch safe rather than an overrun.
- **Holding component pointers across `add`.** A `reg_.get<T>` pointer is invalidated by
  a later `add<T>` (vector growth, ch39). Don't cache them across spawns.
- **Cross-game reuse.** Linking `iso_core` for A\*/TileMap is fine for generic
  algorithms, but it hints those belong in engine-core — a clean-up exercise.

## 10. Glossary

- **Integration** — wiring standalone systems together into a working whole.
- **Data-parallel / disjoint writes** — many tasks writing separate memory: race-free
  without locks.
- **Per-frame scratch** — memory whose lifetime is one frame (the FrameAllocator).
- **Composition over rebuild** — reusing iso projection + A\* rather than reimplementing.

## 11. Exercises

1. **Promote A\*.** Move `astar` into `src/engine/` so games don't depend on
   `games/iso`. What's the minimal interface?
2. **Per-agent goals.** Give each agent its own goal (right-click adds a waypoint) instead
   of one shared goal.
3. **Parallel broadphase.** Add simple agent-avoidance and parallelize the neighbor
   check with `parallel_for` — mind the write-disjointness rule.
4. **Hot-reload live.** Run `--colony`, edit `assets/colony_agent.hrt` (or regenerate it
   differently) while it runs, and watch every agent's sprite change — D in action.

---

### What this closes

The colony sim turns A–F from "built and tested" into "used in a real game," which was
the one genuine piece of tech debt left after the M0–M5 + webserver + A–F program. The
engine isn't just a pile of subsystems anymore — it composes.
