# M4 — Isometric Simulation (farm builder) — Design Spec

> Date: 2026-06-20 · Milestone M4 · Branch `feat/m4-iso-sim`
> Requirement source: `requirements.md` §6 (roadmap), §8 (M4 detail), §10 (acceptance).

## 1. Goal & scope

Build the first **simulation** game on the engine: a small **isometric farm builder**.
It is the milestone that introduces four subsystems the engine has not had yet:

1. **Isometric projection** — a 2:1 diamond grid drawn entirely by hand into the
   2D framebuffer (no 3D rasterizer; this is a *projection*, not real 3D).
2. **Depth sorting** — the painter's algorithm with an isometric depth key so tall
   objects correctly occlude things behind them.
3. **A\* pathfinding** — a hand-written grid path planner; a farmer NPC walks to a
   clicked tile, routing around obstacles.
4. **A small ECS + serialization** — dynamic things (trees, rocks, the farmer)
   live in a tiny sparse-set entity system; the whole world saves/loads to a file
   through the `assets` seam.

**Theme:** farm builder. Terrain tiles (grass / soil / water / stone path) form the
floor; placeable objects (tree / rock / house / fence / wheat) sit on top; one
**farmer** walks the map via A\*.

**Non-goals (M4):** real 3D (that is M3, done); multiple NPCs / crop growth timers
/ economy (possible exercises); networking; the web build (M5).

## 2. Acceptance (maps to `requirements.md` §6 deliverable + §8)

- [ ] **Tile map:** an isometric grid renders with distinct terrain types.
- [ ] **Place tiles/objects:** hover highlights a tile; a brush places terrain or
      objects; a bulldoze brush removes objects. Mouse-driven.
- [ ] **Depth-sort correct:** a tall object in front visually occludes one behind it;
      the farmer is drawn in the right order relative to objects.
- [ ] **A\* pathfinding:** right-click commands the farmer; it walks the shortest
      route around blocking objects/water; no path → no move.
- [ ] **Save/load:** save the world to a file and reload it byte-faithfully.
- [ ] **No SDL above platform; no blocking loop; I/O via `assets`.** (M5-readiness.)
- [ ] **Tests green, no leaks, warning-clean.**

## 3. Architecture (one-directional: game → engine → platform)

```
src/engine/
  iso.hpp            PURE projection math (grid<->screen, depth key). Header-only,
                     no SDL/no render — usable from tests.
  assets.{hpp,cpp}   + write_file()  (the save half of the I/O seam)

src/games/iso/                       ── all PURE of SDL ──
  ecs.hpp            small sparse-set ECS: World + component Pools (header-only)
  tilemap.{hpp,cpp}  Terrain grid + walkability of terrain
  pathfind.{hpp,cpp} A* on a grid via a walkable(x,y) callback
  farm.{hpp,cpp}     the SIM MODEL: TileMap + World + occupancy + farmer;
                     verbs place/remove/command_farmer/update. No rendering.
  serialize.{hpp,cpp} save_farm()/load_farm() — text format, via byte buffers
  iso_render.{hpp,cpp} draws a Farm into Renderer2D (engine only): ground tiles,
                     depth-sorted objects/farmer, hover highlight, HUD
  iso_scene.{hpp,cpp}  Scene: maps input -> Farm verbs + camera; ties render

src/main.cpp         + `--iso` dispatch
tests/test_iso.cpp   iso round-trip · A* cases · ECS · serialize round-trip · depth order
```

**Layering rules honored:** the *model* (`farm`, `tilemap`, `ecs`, `pathfind`,
`serialize`, `iso`) is pure C++ and SDL-free, so it is unit-tested without a window.
Only `iso_render` and `iso_scene` touch the engine renderer, and nothing touches SDL.

## 4. Isometric projection (`iso.hpp`)

Classic **2:1 dimetric** tiles: `TILE_W = 64`, `TILE_H = 32`. A grid cell `(gx, gy)`
maps to the **screen center of its diamond**:

```
sx = ox + (gx - gy) * (TILE_W / 2)
sy = oy + (gx + gy) * (TILE_H / 2)
```

`(ox, oy)` is the camera offset (pan). The inverse (for mouse picking) solves the
2×2 system:

```
fx = (sx - ox) / (TILE_W/2)       // = gx - gy
fy = (sy - oy) / (TILE_H/2)       // = gx + gy
gx = (fx + fy) / 2 ;  gy = (fy - fx) / 2     // then floor() to the tile
```

**Depth key** (painter's order, back-to-front): `key(gx, gy) = gx + gy`. Larger sum =
closer to the camera = drawn later. For moving agents the key uses *fractional*
grid coords so the farmer sorts correctly mid-tile. Tie-break by `gy` then insertion
order for determinism. (Strictly-correct ordering for objects with multi-tile
footprints needs a topological sort; we keep **1×1 footprints**, for which the sum
key is correct — documented as an exercise.)

## 5. TileMap (`tilemap.{hpp,cpp}`)

```cpp
enum class Terrain : uint8_t { Grass, Soil, Water, Path };
class TileMap {
  int w_, h_; std::vector<Terrain> t_;          // dense w*h, row-major
  Terrain at(x,y); void set(x,y,Terrain);        // bounds-checked
  bool in_bounds(x,y);
  bool terrain_walkable(x,y);                    // everything except Water
};
```

Terrain is a **dense grid** (every cell has exactly one), so it is *not* an entity —
that is the right data model and is contrasted with the ECS in the chapter.

## 6. Small ECS (`ecs.hpp`)

A genuine but minimal **sparse-set** ECS — enough to teach the data-oriented idea
(components stored apart from entities; systems are free functions over pools)
without archetype machinery.

```cpp
using Entity = uint32_t;                 // 0 = invalid (kInvalid)
template<class T> class Pool {            // sparse set
  std::vector<Entity> dense_;             // entity id at each slot
  std::vector<T>      data_;              // component at each slot
  std::unordered_map<Entity,size_t> at_;  // entity -> slot
  bool has(e); T& add(e,T); T* get(e); void remove(e);  // swap-and-pop
  // iterable: data()/entities()
};
struct Position  { float x, y; };                 // grid coords (farmer fractional)
struct Renderable{ ObjKind kind; gfx::Color tint; };
struct Mover     { std::vector<Vec2i> path; size_t idx; float speed; };
class World {
  Entity create(); void destroy(Entity);          // removes from all pools
  Pool<Position> positions; Pool<Renderable> renderables; Pool<Mover> movers;
  const std::vector<Entity>& alive() const;
};
```

`ObjKind { Tree, Rock, House, Fence, Wheat }` with `blocks(kind)` (wheat is passable;
the rest block movement). `Vec2i { int x,y; }` is shared with pathfinding.

## 7. A* pathfinding (`pathfind.{hpp,cpp}`)

8-connected grid A\*, **octile** heuristic, **no corner cutting** (a diagonal step is
allowed only if both orthogonal neighbors are walkable). Walkability is supplied by a
`std::function<bool(int,int)>` so the planner is decoupled from the farm.

```cpp
std::vector<Vec2i> astar(int w, int h, Vec2i start, Vec2i goal,
                         const std::function<bool(int,int)>& walkable);
// returns cells start..goal inclusive; empty if unreachable; {start} if start==goal.
```

Costs: orthogonal `1.0`, diagonal `√2`. `std::priority_queue` open set, `g/f` arrays,
`came_from` for reconstruction. Tested for optimal length, detours, and no-path.

## 8. Farm model (`farm.{hpp,cpp}`)

The sim. Holds the truth; renderer/scene only read it.

```cpp
class Farm {
  TileMap map_;
  World   world_;
  std::vector<Entity> occ_;     // w*h occupancy: object entity on each tile (0 = none)
  Entity  farmer_ = kInvalid;
public:
  Farm(int w, int h);            // default = grass field
  // editing
  void set_terrain(x,y,Terrain);
  Entity place_object(x,y,ObjKind);   // one object per tile; replaces existing
  void   remove_object(x,y);
  Entity object_at(x,y);
  // simulation
  bool walkable(x,y) const;           // terrain not water AND tile not blocked
  bool command_farmer(int gx,int gy); // A* from farmer cell; sets Mover path
  void update(double dt);             // movement system advances the farmer
  // access for render + serialize
};
```

One object per tile keeps depth-sort and placement simple. `occ_` is a derived
acceleration grid (rebuilt on load) for O(1) walkability and placement queries; the
ECS `World` remains the source of truth.

**Movement system** (in `update`): the farmer's `Mover` interpolates toward the next
cell center at `speed` tiles/sec; on arrival it advances `idx`; path done → idle.

## 9. Serialization (`serialize.{hpp,cpp}`)

Human-readable, versioned **text** format (teachable; diff-able), exchanged as
`std::vector<uint8_t>` so the file layer (`assets`) stays format-agnostic.

```
FARM 1
SIZE <w> <h>
TILES                       # h lines of w chars: '.'=grass ':'=soil '~'=water '#'=path
<row0>
...
OBJECTS <n>                 # n lines: <kindChar> <x> <y>
<...>
FARMER <gx> <gy>            # -1 -1 if none
```

`save_farm(farm) -> bytes` and `load_farm(farm, bytes) -> bool` round-trip exactly
(tested). Loading rebuilds the ECS + occupancy from scratch.

**I/O seam extension:** add `assets::write_file(path, bytes)` (standard streams,
mirrors `load_file`). Web note: Emscripten persists writes via IDBFS/localStorage at
M5 — confined to this one seam, no caller changes.

## 10. Rendering (`iso_render.{hpp,cpp}`) — engine renderer only

Per frame:
1. Clear to a sky color.
2. **Ground**: iterate `gy` outer, `gx` inner (back-to-front) → fill each tile's
   diamond with its terrain color + a subtle edge; overlay the hovered tile.
3. **Objects + farmer**: collect `(Position, Renderable)` into a vector, sort by
   `iso::depth_key`, draw each back-to-front. Static objects draw as **isometric
   boxes / shapes** (tree = trunk box + canopy; rock = grey box; house = big box +
   roof; fence = thin tall box; wheat = small gold tufts), shaded top/left/right for
   fake volume. The farmer is a small two-tone figure with a ground shadow.
4. **HUD**: brush, mode, entity count, controls, save/load hints (`draw_text`).

New private helper: `fill_diamond` + `draw_iso_box` (both write via `Renderer2D`).

## 11. Scene & controls (`iso_scene.{hpp,cpp}`)

`IsoScene : engine::Scene`. All input handled in `update(dt,input)` (matches existing
scenes; place/command are idempotent so the fixed-step edge model is safe). Camera is
a 2D pan offset `(ox, oy)`.

| Input | Action |
|------|--------|
| Mouse move | hover-highlight tile under cursor |
| `1..0` / `Tab` | select brush (Grass/Soil/Water/Path/Tree/Rock/House/Fence/Wheat/Bulldoze) |
| Left click | apply brush to hovered tile |
| Right click | command farmer to walk to hovered tile (A\*) |
| Arrows / WASD | pan camera |
| `F5` save · `F9` load | serialize via `assets` (`save/farm.txt`) |
| `R` | reset to default farm |
| `Esc` | quit |

Window: 960×600, smooth + HiDPI, `--iso`. (Save uses `F5`/`F9`; new keys
`Key::F1..F9`-style — reuse existing keys where possible, add `Num0`, `F5`, `F9`,
`P` as needed; backend scancode table extended.)

## 12. Build & test wiring

- New static lib **`iso_core`** = `tilemap.cpp pathfind.cpp farm.cpp serialize.cpp`
  (PUBLIC include `src`, links `engine_flags`). Pure, no SDL.
- **`test_iso`** links `iso_core` + `engine_flags`; registered as ctest `iso`.
- `demo` links `iso_core` and compiles `iso_render.cpp iso_scene.cpp`.
- `assets.cpp` already in `demo`; `write_file` added there (and `test_iso` compiles
  `assets.cpp` directly for the serialize round-trip if it touches disk — but the
  round-trip test uses in-memory byte buffers, so no disk needed).

## 13. Risks / decisions

- **Depth-sort correctness:** restricted to 1×1 footprints where the `gx+gy` key is
  provably correct; multi-tile/topological sort is an exercise. Honest in the doc.
- **Input edge model:** reuse the existing per-`update` edge handling; place/command
  are idempotent, so double/zero fire is harmless (noted in ch like before).
- **Save path on web:** writing is desktop-real now; web persistence is an M5 seam
  detail, not an engine change.
- **Scope/size:** six model files + render + scene is large but cleanly separated and
  each unit is independently testable — matches the engine's module discipline.

## 14. Guidebook (deliverable)

Chapters, same shape (concept → walkthrough → run → pitfalls → exercises):
- **26** Isometric projection (the diamond grid, the math, picking)
- **27** TileMap & depth sorting (painter's algorithm, the iso key)
- **28** A small ECS (sparse sets, components vs entities, systems)
- **29** A\* pathfinding (open/closed sets, octile heuristic, no corner cutting)
- **30** Save/load & serialization (the text format, the write seam, web note)
- **31** M4 acceptance (the farm in action, the controls, what we built)

Update `00-overview.md` reading order + roadmap and `README.md`.
