# Chapter 31 — M4 Acceptance: The Isometric Farm Sim

> **What this is.** The wrap-up of M4. We assemble the five subsystems from Chapters
> 26–30 into one interactive sandbox (`--iso`), walk the controls, verify the
> milestone's acceptance criteria against `requirements.md`, and reflect on the
> architecture. Code: `src/games/iso/iso_scene.{hpp,cpp}` is the thin shell that wires
> input to the model and the model to the renderer.

---

## 1. What we built

```
                 input (mouse + keys)
                        │
            ┌───────────▼───────────┐
            │      IsoScene         │  ch31 — maps input → verbs, draws + HUD
            └───────────┬───────────┘
                        │ verbs (place / command / save)         reads
        ┌───────────────▼───────────────┐        ┌───────────────────────┐
        │            Farm                │◄───────│      iso_render        │ ch27
        │  TileMap + World + occupancy   │        │ diamonds, depth sort   │
        │  + farmer  (ch27/28)           │        └───────────────────────┘
        └───┬─────────┬─────────┬────────┘
            │         │         │
        TileMap     World     A*  ……… serialize (ch30)
        (ch27)     (ch28)   (ch29)
                        │
                 engine/iso.hpp  (ch26 — projection math)
```

Everything below `IsoScene` is **pure, SDL-free logic** compiled into `iso_core` and
unit-tested without a window. `IsoScene` and `iso_render` are the only files that
touch the engine renderer; *nothing* in M4 touches SDL. That layering is what we
verify in §4.

## 2. The scene is a thin shell

`IsoScene` holds almost no logic — it **translates**. Each frame's `update` reads the
normalized `InputState` and calls `Farm` verbs:

- **Brush keys `1`–`0` / `Tab`** pick what the left mouse paints (terrain or object or
  bulldoze).
- **Left-drag** paints, re-applying only when the cursor enters a *new* tile so we
  don't churn entities every frame.
- **Right-click** → `farm_.command_farmer(tile)` (A\*; flashes "no path" on failure).
- **Arrows/WASD** pan the `Camera2D` offset (clamped so picking math stays in range).
- **F5/F9/R** → save / load / reset, save and load going through the asset seam.
- Then `farm_.update(dt)` advances the simulation (the farmer walks).

`render` computes the hovered tile (`screen_to_grid` on the cursor), calls
`render_farm`, and draws the HUD. The split mirrors every other scene: logic in
`update`, drawing in `render`.

## 3. Controls (the on-screen HUD says this too)

| Input | Action |
|-------|--------|
| `1` `2` `3` `4` | terrain brush: grass · soil · water · path |
| `5` `6` `7` `8` `9` | object brush: tree · rock · house · fence · wheat |
| `0` | bulldoze (remove the object on a tile) |
| `Tab` | cycle to the next brush |
| **Left mouse** | paint the current brush on the hovered tile (drag to paint a line) |
| **Right mouse** | send the farmer walking to the hovered tile (A\*) |
| Arrows / `WASD` | pan the camera |
| `F5` / `F9` | save / load (`assets/farm_save.txt`) |
| `R` | reset to the default starter farm |
| `Esc` | quit |

Run it:

```sh
cmake --build build
./build/demo --iso
```

## 4. Acceptance — checked against `requirements.md` §6/§8/§10

- [x] **Tile map renders isometrically** with distinct terrain (grass/soil/water/path
      diamonds). — Ch26/27; visible immediately on launch.
- [x] **Place tiles & objects with the mouse**; hover highlight; bulldoze removes. —
      `apply_brush`, left-drag painting.
- [x] **Depth-sort correct.** A near tree occludes a far one; the farmer passes
      *behind* then *in front of* the house. — Ch27 painter's algorithm with the
      `gx+gy` key.
- [x] **A\* pathfinding.** Shortest route around obstacles; "no path" when walled in;
      re-plans around newly placed blockers. — Ch29.
- [x] **Save / load via serialization**, byte-faithful and transactional. — Ch30,
      F5/F9.
- [x] **Small ECS / entity management.** Sparse-set `World` with Position/Renderable/
      Mover pools and systems as free functions. — Ch28.
- [x] **No SDL above the platform layer; no blocking loop; I/O via `assets`.** —
      verified below.
- [x] **Tests green, no leaks, warning-clean.** — `ctest` 6/6; `leaks` 0; `-Wall
      -Wextra -Wpedantic` clean; ASan+UBSan run of the model clean.

### Architecture invariants (the M5-readiness gates)

```sh
grep -rn "SDL_\|#include <SDL" src/games/iso src/engine/iso.hpp   # → nothing
grep -rn "fopen\|ifstream\|ofstream" src/games/iso                # → nothing (I/O via assets)
```

The iso simulation has zero SDL references and does no direct file I/O — saves and
loads flow through `assets::write_file`/`load_file`. The game loop is still the
platform's `tick(dt)`; `IsoScene` never blocks. M4 keeps every web-portability rule
intact.

### Test coverage (`tests/test_iso.cpp`, ctest target `iso`)

`test_projection` (grid↔screen round-trip, depth-key ordering) · `test_ecs` (sparse-set
add/overwrite/swap-and-pop/clear) · `test_tilemap` (bounds, walkability sentinel) ·
`test_astar` (straight, diagonal, start==goal, unreachable, no-corner-cut, detour) ·
`test_farm` (place/replace/remove, walkability, command + arrival, blocked target) ·
`test_serialize` (byte-faithful round-trip, transactional malformed load).

## 5. What this milestone taught

- **Coordinate transforms** both ways, and why floats let agents glide (Ch26).
- **Depth without a z-buffer** — the painter's algorithm and the magic of `gx+gy`
  (Ch27).
- **Data-oriented design** — a real sparse-set ECS, components vs. entities vs.
  systems (Ch28).
- **The single most useful game algorithm**, A\*, with an honest, admissible heuristic
  and the corner-cutting rule (Ch29).
- **Persistence done safely** — versioned text, defensive parsing, transactional load,
  and a write seam that stays web-portable (Ch30).

## 6. Honest limitations (good exercise fuel)

- **1×1 footprints only.** The `gx+gy` sort is provably correct only for single-tile
  objects; multi-tile buildings need a topological sort (Ch27 ex. 3).
- **One farmer, re-plan-from-scratch.** Fine here; thousands of agents would want flow
  fields (Ch29 ex. 4).
- **No zoom, no undo, no crop growth.** All are clean extensions on top of the model.
- **Web persistence** is an M5 seam detail (`write_file`), not wired yet.

## 7. Where M4 leaves the project

Milestones M0–M4 are complete: an engine foundation, chess, an FPS raycaster, a real
3D core, a 3D sandbox, and now an isometric simulation — all on one hand-written
engine, SDL confined to the platform shim, every pixel drawn by us. The only milestone
left is **M5: the web port via Emscripten**, which — by design since Chapter 00 —
should add a `backend_web.cpp` and a build target *without rewriting any engine or
game code*. The invariants we just re-verified are exactly what make that possible.
