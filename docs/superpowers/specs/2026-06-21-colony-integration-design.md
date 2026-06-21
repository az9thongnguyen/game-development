# Colony — Engine-Core Integration Game — Design Spec

> Date: 2026-06-21 · Addresses "integration debt" (item 1) · Branch `feat/colony`
> A NEW iso game built ON the engine-core subsystems A–F, so they're used for real
> (the audit found A/B/C/D had 0 game users). Existing games are left untouched.

## 1. Goal

Build a small **isometric agent sandbox** (`--colony`) whose *entire* implementation
stands on the engine-core systems, proving they compose:

- **B — ECS** (`ecs::Registry`): agents and props are entities with components — NOT a
  bespoke struct list (unlike the M4 farm's `games/iso/ecs.hpp`).
- **C — Jobs** (`jobs::JobSystem`): agent movement is advanced with `parallel_for`
  (multicore on desktop, synchronous on web).
- **A — Allocators** (`mem::FrameAllocator`): the per-frame depth-sort drawable list is
  built in frame scratch (the adoption promised in ch37/47).
- **D — Asset cache + hot reload**: a self-generated agent sprite (`.hrt`) is loaded via
  `assets::AssetCache` and `reload_changed()` is polled each frame (edit the file → it
  updates live).
- **F — GUI** (`ui::Context`): an immediate-mode panel drives the sandbox.

Reuses `engine/iso.hpp` (projection) and `iso::astar`/`iso::TileMap` (A* is generic via
a walkable callback). (A* arguably belongs in engine-core; moving it is a noted exercise.)

## 2. Architecture (model pure, scene renders)

```
src/games/colony/
  colony.hpp / colony.cpp     Sim (MODEL, testable, no SDL/renderer):
    components GridPos{x,y} · Visual{color,is_agent} · Agent{path,idx,speed}
    ecs::Registry reg_ · iso::TileMap map_ · jobs::JobSystem jobs_
    spawn_agent/spawn_prop · set_goal(gx,gy) [A* per agent] · walkable · update(dt) [parallel_for]
  colony_scene.hpp / .cpp     Scene (RENDER): ui::Context, mem::FrameAllocator,
    assets::AssetCache (agent sprite + hot reload); draws terrain + depth-sorted
    entities (frame-allocated drawable list); HUD; click→goal/spawn
tests/test_colony.cpp         CTest 'colony' (the Sim: ECS + jobs movement)
docs/book/50-engine-core-integration.md
```

CMake: `colony_core` STATIC (colony.cpp) PUBLIC include `src`, links
`ecs_core jobs_core iso_core` (+ `Threads` transitively via jobs). `test_colony` links
`colony_core`. `demo` adds `colony_scene.cpp`, links `colony_core mem_core ui_core` (+
already-linked renderer/asset via the demo). `--colony` dispatch in main.

## 3. The Sim (B + C)

```cpp
namespace colony {
struct GridPos { float x = 0, y = 0; };
struct Visual  { gfx::Color color = 0xFFFFFFFF; bool is_agent = false; };
struct Agent   { std::vector<iso::Vec2i> path; std::size_t idx = 0; float speed = 3.0f; };

class Sim {
public:
    Sim(int w, int h);
    int  width() const; int height() const;
    const iso::TileMap& map() const;
    ecs::Registry& registry();                 // for the renderer/tests to view()
    ecs::Entity spawn_agent(int x, int y, gfx::Color);
    ecs::Entity spawn_prop(int x, int y, gfx::Color);
    bool walkable(int x, int y) const;          // terrain not water
    void set_goal(int gx, int gy);              // A* a path for EVERY agent
    void update(double dt);                      // parallel_for advances all agents
    int  agent_count() const;
    void reset_default();
};
}
```

`update`: snapshot agent entities via `reg_.view<Agent,GridPos>`, then
`jobs_.parallel_for(n, advance_one, grain)`. Each task touches only its own entity's
`Agent`/`GridPos` (distinct dense-array slots) and does NOT add/remove entities, so the
shared pools are read concurrently and written at disjoint slots — data-race-free
(validated by ThreadSanitizer). Movement math = the farm's: glide to `path[idx]` at
`speed` tiles/s, snap + advance on arrival.

## 4. The Scene (A + D + F)

- **D:** ctor ensures `assets/colony_agent.hrt` exists (generate a tiny procedural
  sprite via `assets::write_file` if missing), registers an `Image` loader on an
  `AssetCache`, and `load<gfx::Image>`s it. `render` calls `cache.reload_changed()` so
  editing the file hot-reloads the agent sprite live.
- **A:** each `render` does `frame_.flip()` then allocates the depth-sort `Drawable`
  array from the `FrameAllocator` (per-frame transient scratch), sorts it, draws.
- **F:** a `ui::Context` panel: **Spawn agent**, **Scatter** (random goals), a **speed**
  slider, a **run** checkbox (pause/resume), **Reset**, live counts. Click the world to
  set a goal for all agents (or spawn with a modifier). `hovering_ui()` gates world clicks.
- Terrain drawn back-to-front (reusing the iso diamond fills); entities drawn
  depth-sorted by `iso::depth_key`; agents blit the cached sprite, props draw as iso boxes.

## 5. Correctness focus
- ECS: spawn creates a valid entity with the right components; reset clears the registry.
- Jobs: `parallel_for` movement gives the SAME result as a serial loop (determinism);
  no data race (TSan); agents reach their goals.
- set_goal: every agent gets an A* path (or none if unreachable); idle agents at goal.
- FrameAllocator: drawable list fits / flips each frame; no leak (RAII).
- Asset cache: sprite loads; missing/again returns cached; hot-reload swaps in place.

## 6. Tests (`tests/test_colony.cpp`)
spawn agents/props (entity + components present); `set_goal` populates paths; `update`
advances agents and they arrive at the goal over N steps; serial-vs-parallel result
identical (run with worker_count 0 and default, compare); reset clears. (A/D/F are
already unit-tested in test_mem/test_assets/test_ui; the scene is verified by offline
render.)

## 7. Guidebook
- **50 — Engine-core integration: the colony sim** — how A (frame scratch) + B (ECS) +
  C (parallel_for) + D (cached/hot-reloaded sprite) + F (GUI) compose into one game,
  the data-race-free parallel ECS pattern, and what was reused vs. engine-core. Update
  overview/README.

## 8. Risks
- Parallel ECS writes: safe only because no structural changes (add/remove) happen
  during the `parallel_for` and each task writes disjoint slots — documented + TSan-checked.
- Cross-game dependency on `iso_core` (A*/TileMap): acceptable for reusable algorithms;
  note that A* belongs in engine-core (exercise).
- Scope: a full new game; kept focused (no physics — that's the editor's demo).
