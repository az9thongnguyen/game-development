# Sandbox Editor — Design Spec (v1)

> Sub-project 2 of the Platform Expansion roadmap (Track B, Mini Studio family).
> Predecessor: Texture Lab v1 (merged `29b3be6`). Sibling design doc:
> `2026-07-11-platform-expansion-and-mini-studio-design.md`.

## 1. Goal

A **2D top-down sandbox** where you place actors by drag-and-drop, attach
**declarative behaviors** to them (no code, no embedded language), and press
**Play** to watch a deterministic simulation run — then **Stop** to snap back to
the placed state and keep editing. This is the "kéo thả + sandbox + logic cơ bản"
the user asked for, resolved at the **declarative-first** rung agreed earlier:
data-driven behavior components + one event→action rule (`OnOverlap`), all riding
the existing generic `ecs::Registry`. An embedded scripting VM/Lua (rung 3) stays
explicitly deferred — see §9.

Non-goals for v1: 3D placement (the `viz3d` editor already does 3D), textured
sprites (solid colors now; Texture Lab `.hrt` integration is v2), undo/redo, an
event bus, multi-select, physics collision response beyond wall-bounce.

## 2. Why this shape

- **Declarative-first was the chosen rung.** Behaviors are data on entities; the
  "program" is the set of components you attach, not a script. This is the lowest
  rung that still delivers real interactive logic, and it is 100% unit-testable
  with no window.
- **2D top-down, not 3D.** The novelty here is the *behavior + Play/Stop* layer,
  which is orthogonal to 2D-vs-3D. 2D keeps the new surface minimal: placement is
  `mouse → world xy` (no ray-plane), picking is point-in-AABB (no ray-sphere),
  rendering is `fill_rect`/`fill_circle` via `Renderer2D`. The 3D editor
  (`viz3d`) already exists for anyone who wants 3D placement.
- **Reuse over rebuild.** Built on `ecs::Registry` (typed pools + `view<...>`),
  `ui::Context` (immediate-mode inspector/palette), `Renderer2D`, and the
  `assets::` seam. The only genuinely new code is the behavior components, the
  `tick` systems, the scene serializer, and the scene's drag/select glue.

## 3. Architecture

Two units, mirroring every other game in the repo (pure core + a thin scene):

```
src/games/sandbox/
  world.hpp / world.cpp        pure: components, Archetype, World, tick(dt), reap
  serialize.hpp / serialize.cpp pure: World <-> text (save/load AND play/stop snapshot)
  sandbox_scene.hpp / .cpp     Scene: palette, drag-drop, select, inspector, Play/Stop, draw
tests/test_sandbox.cpp         CTest suite `sandbox` (headless, no SDL)
```

- **`sandbox_core`** static lib = `world.cpp` + `serialize.cpp` (SDL-free, like
  `iso_core`/`studio_core`). `test_sandbox` links it (and compiles nothing that
  needs a window).
- **`sandbox_scene.cpp`** is added to the `demo` target only; it is the sole file
  that touches `ui::Context`/`Renderer2D`/`platform::InputState`.

### 3.1 Data flow

```
edit mode:  input --> SandboxScene (palette drag / select / inspector) --> World (mutate components)
play mode:  App::update(fixed dt) --> World::tick(dt) --> systems mutate components --> reap
every frame: SandboxScene::render --> draw entities (Transform+Body+Sprite) + UI panels
Play  = snapshot = serialize(World) -> string (kept in memory)
Stop  = deserialize(snapshot) -> World   (restores exactly the placed state)
Save  = serialize(World) -> assets::write_file("sandbox/<name>.scene")
Load  = assets::load_file -> deserialize -> World
```

The **simulation runs in `Scene::update(dt)`** (fixed 1/60 s, deterministic) and
only while playing. **Editing + drawing run in `Scene::render`** (immediate-mode,
studio-style). This is the exact split `scene.hpp` documents.

## 4. Components (all plain structs on `ecs::Registry`)

| Component | Fields | Meaning |
|---|---|---|
| `Transform2D` | `float x, y, rot=0, scale=1` | center position (world px), rotation (rad), uniform scale |
| `Body` | `float w=24, h=24` | AABB full size (px) for draw / pick / overlap, centered on Transform |
| `Sprite` | `gfx::Color color; bool round=false` | v1 solid fill; `round` → circle instead of rect |
| `Mover` | `float vx=0, vy=0` | velocity px/s. system: `pos += v*dt` |
| `Spinner` | `float omega=0` | angular velocity rad/s. system: `rot += omega*dt` |
| `Bouncer` | *(tag; no fields)* | reflect the entity's `Mover` velocity at world bounds, clamp inside |
| `Lifetime` | `float ttl` | seconds left; system `ttl -= dt`, mark dead at `<=0` |
| `Spawner` | `float interval, timer=0; Archetype proto` | every `interval` s spawn `proto` at spawner's pos |
| `Tag` | `int id=0` | small integer label (0 = untagged); read by triggers |
| `OnOverlap` | `int other_tag; Action action` | when this entity's AABB overlaps any entity whose `Tag.id == other_tag`, run `action` |

`Action = { DestroySelf, DestroyOther, SpawnProto }` (+ `Archetype proto` field on
`OnOverlap` for `SpawnProto`). `Spawner.proto` and `OnOverlap.proto` are **flat**
`Archetype`s (no nested spawner/trigger) — this bounds recursion in v1.

### 4.1 `Archetype` — the palette entry / spawn template

```cpp
struct Archetype {
    std::string name  = "actor";
    gfx::Color  color = gfx::rgb(220, 200, 120);
    float       w = 24, h = 24;
    bool        round = false;
    // optional behaviors (attached iff the flag is set):
    bool  mover   = false; float vx = 0, vy = 0;
    bool  spinner = false; float omega = 0;
    bool  bouncer = false;
    bool  lifetime= false; float ttl = 2.0f;
    int   tag     = 0;
};
```

`World::spawn(const Archetype&, float x, float y) -> ecs::Entity` creates an
entity, always adds `Transform2D{x,y}`, `Body{w,h}`, `Sprite{color,round}`, then
adds each flagged behavior. This is the single funnel used by drag-drop, spawners,
triggers, and deserialization.

The scene ships a small fixed **palette** of archetypes (v1): `Ball`
(mover+bouncer, round), `Spinner` (spinner, square), `Emitter` (a spawner of small
`Ball`s), `Coin` (tag=1, round), `Sweeper` (mover + `OnOverlap{other_tag=1,
DestroyOther}` — eats coins).

## 5. `World::tick(float dt)` — deterministic system order

A single `tick` runs the systems in a fixed order, accumulating structural changes
in a **command buffer** applied at the end (so no system removes/adds an iterated
component mid-`view`, which `registry.hpp` forbids):

```
1. spawners   : timer += dt; while (timer >= interval) { timer -= interval; cmd.spawn(proto, pos) }
2. movers     : pos.x += vx*dt; pos.y += vy*dt
3. spinners   : rot += omega*dt
4. bouncers   : if AABB crosses a world edge → clamp inside + flip that velocity axis
5. lifetime   : ttl -= dt; if ttl <= 0 → cmd.destroy(e)
6. overlaps   : for each OnOverlap e, for each entity o with Tag==other_tag,
                if AABB(e) overlaps AABB(o): apply action via cmd (destroy/spawn), break
7. reap       : apply cmd.destroys (dedup) then cmd.spawns
```

Determinism: same world + same `dt` sequence ⇒ identical result. No RNG in the
sim. `tick` never reads input or wall-clock. Overlap is O(n²) all-pairs —
`// ponytail: O(n^2) overlap, fine for a sandbox; grid-hash if entity counts grow`.

Fixed-timestep note: `App` already calls `update` at a fixed 1/60 s. The scene
forwards that `dt` to `tick` only when `playing_`. `Spawner.timer`/`Lifetime.ttl`
therefore advance in exact 1/60 increments.

## 6. Serialization (one format, three uses: save, load, snapshot)

Text, hand-parsed, in the `recipe`/`iso::serialize` style — tolerant of unknown
keys, one entity per line:

```
sandbox1
bounds 936 560
e x=100 y=100 w=24 h=24 color=ffcc40 round mover=40,30 bouncer tag=1
e x=300 y=200 w=30 h=30 color=40ccff spinner=2.0
e x=60  y=60  spawner=1.5:ball_proto onoverlap=1:destroyother
```

- Header line `sandbox1` (format id) + `bounds W H`.
- One `e ...` line per entity; tokens are `key` or `key=value`.
- Behaviors that carry a proto (`spawner`, `onoverlap` with `SpawnProto`) encode
  the proto inline as a compact sub-record (a named mini-archetype, or a nested
  `{...}` group — see the plan for the exact grammar).
- `to_scene(const World&) -> std::string` and
  `from_scene(const std::string&) -> World` round-trip: `from_scene(to_scene(w))`
  reproduces byte-identical `to_scene` output.

**Play/Stop = snapshot** is literally `std::string snap = to_scene(world_)` on
Play and `world_ = from_scene(snap)` on Stop. No separate mechanism.

## 7. Scene interactions (`SandboxScene`)

- **Palette panel** (left): one button per archetype. Clicking arms a "pending
  place" of that archetype; the next left-click on the canvas spawns it there.
  (v1 uses click-to-arm + click-to-place — a true press-drag-drop ghost is a v1.1
  polish; the interaction model and picking are the same.)
- **Select**: left-click on an entity (point-in-AABB, topmost = last drawn) selects
  it; drag moves the selected entity's `Transform2D`. Click empty space deselects.
- **Inspector panel** (right): when an entity is selected, show/edit `Transform2D`
  (x/y/rot/scale sliders), `Sprite.color` (cycle presets), `Body` (w/h sliders),
  and checkboxes to toggle each behavior on/off with a param slider each.
- **Play/Stop button** (top): toggles `playing_`. Play snapshots; Stop restores.
  While playing, the inspector is read-only (editing is an edit-mode action).
- **Save/Load**: `F5` writes `assets/sandbox/scene.scene`; `F9` reloads it.
  (Fixed filename in v1; a name field is v2, matching iso's F5/F9.)
- `ui_.hovering_ui()` gates canvas clicks so a palette/inspector click never also
  places/selects on the canvas (same guard studio relies on).

## 8. Testing (`tests/test_sandbox.cpp`, CTest suite `sandbox`)

Pure-core, headless, assert-based `CHECK` (same harness as `test_studio`):

1. **spawn attaches components** — `Archetype` flags map to exactly the expected
   pools; Transform/Body/Sprite always present.
2. **mover integrates** — one `tick(dt)` moves pos by `v*dt`; determinism: two
   identical worlds ticked the same ⇒ identical positions.
3. **spinner** — `rot` advances by `omega*dt`.
4. **bouncer reflects** — a mover crossing a wall clamps inside and flips that
   velocity component; the other component is unchanged.
5. **lifetime despawns** — after `ttl` worth of ticks the entity is gone; alive
   count drops by exactly one.
6. **spawner spawns at interval** — after `n*interval` seconds, exactly `n` new
   entities exist at the spawner's position.
7. **overlap trigger** — a `Sweeper` overlapping a `Coin` (tag 1) with
   `DestroyOther` removes the coin and not the sweeper; no overlap ⇒ nothing dies.
8. **serialize round-trip** — `from_scene(to_scene(w))` reproduces identical
   `to_scene` text *and* an identical `tick` trajectory (proves snapshot fidelity).
9. **snapshot restores** — build a world, snapshot, `tick` it 100×, restore from
   snapshot ⇒ equals the pre-tick world (Play/Stop correctness).

## 9. Deferred (v2+), on purpose

- **Textured sprites** from the Texture Lab collection (`.hrt` load into `Sprite`)
  — the two Mini-Studio modules meet here; deferred to keep v1 about behaviors.
- **3D sandbox** (place on a ground plane via `pick::ray_plane_y`, reuse
  `viz3d::Editor`) — a separate scene later.
- **Richer triggers**: `OnTimer`, `OnClick`, `OnKey`; multiple rules per entity;
  an action list. v1 ships exactly one `OnOverlap` rule per entity to prove the
  event→action pattern.
- **Rung 3 — embedded logic language**: only if the declarative rungs hit a real
  ceiling. Hand-written tiny bytecode VM (on-brand/learning) or Lua (1 dep, like
  Drogon in BaaS). Still deferred; nothing in v1 blocks adding it as a `Script`
  component later.
- Undo/redo, multi-select, named save slots, press-drag ghost preview.

## 10. Risks / decisions

- **Structural edits during `view`**: mitigated by the command buffer (§5); tested
  by determinism + snapshot tests.
- **Float determinism across builds**: sim uses only `+ - *` on floats in a fixed
  order — reproducible within a single build (sufficient for snapshot/Play-Stop,
  which is same-build). Cross-build reproducibility is not a v1 goal.
- **Scope**: v1 is one scene + one pure core + 9 tests. It is a complete, testable
  vertical slice; the deferrals in §9 are all additive (new components/scenes),
  none require reworking v1.
