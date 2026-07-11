# Sandbox Editor v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A `--sandbox` 2D top-down editor: drag-drop actors, attach declarative behaviors, Play/Stop a deterministic ECS simulation, save/load scenes.

**Architecture:** Pure `sandbox_core` lib (components + `World::tick` systems + text serializer) on the existing generic `ecs::Registry`, plus a thin `SandboxScene` (palette / select / inspector / Play-Stop) that is the only file touching UI/renderer. Play/Stop reuses the serializer as an in-memory snapshot.

**Tech Stack:** C++20, `ecs::Registry`, `ui::Context`, `gfx::Renderer2D`, `assets::`, CTest. No new dependencies.

Spec: `docs/superpowers/specs/2026-07-11-sandbox-editor-design.md`.

---

## File structure

- Create `src/games/sandbox/world.hpp` / `world.cpp` — components, `Archetype`, `World`, `tick`, command buffer, reap.
- Create `src/games/sandbox/serialize.hpp` / `serialize.cpp` — `to_scene` / `from_scene` (+ archetype token codec, shared).
- Create `src/games/sandbox/sandbox_scene.hpp` / `sandbox_scene.cpp` — the Scene.
- Create `tests/test_sandbox.cpp` — CTest suite `sandbox`.
- Modify `CMakeLists.txt` — `sandbox_core` lib, `test_sandbox` target + `add_test`, add scene to `demo`.
- Modify `src/main.cpp` — `--sandbox` branch.
- Modify `README.md` — run-list line + roadmap row.

---

## Task 1: Components + `World::spawn`

**Files:** Create `src/games/sandbox/world.hpp`, `src/games/sandbox/world.cpp`, `tests/test_sandbox.cpp`. Modify `CMakeLists.txt`.

`world.hpp` (component structs + Archetype + World declaration):

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "engine/color.hpp"
#include "engine/ecs/registry.hpp"

namespace sandbox {

struct Transform2D { float x = 0, y = 0, rot = 0, scale = 1; };
struct Body        { float w = 24, h = 24; };
struct Sprite      { gfx::Color color = gfx::rgb(220,200,120); bool round = false; };
struct Mover       { float vx = 0, vy = 0; };
struct Spinner     { float omega = 0; };
struct Bouncer     {};                       // tag component
struct Lifetime    { float ttl = 2.0f; };
struct Tag         { int id = 0; };

enum class Action { DestroySelf, DestroyOther, SpawnProto };

// Flat template used by palette, Spawner, OnOverlap, and (de)serialization.
struct Archetype {
    std::string name = "actor";
    gfx::Color  color = gfx::rgb(220,200,120);
    float w = 24, h = 24; bool round = false;
    bool  mover = false;   float vx = 0, vy = 0;
    bool  spinner = false; float omega = 0;
    bool  bouncer = false;
    bool  lifetime = false; float ttl = 2.0f;
    int   tag = 0;
};

struct Spawner   { float interval = 1.0f, timer = 0; Archetype proto; };
struct OnOverlap { int other_tag = 0; Action action = Action::DestroySelf; Archetype proto; };

class World {
public:
    float bounds_w = 936, bounds_h = 560;

    ecs::Entity spawn(const Archetype& a, float x, float y);
    void        tick(float dt);
    std::size_t alive() const { return reg.alive(); }
    ecs::Registry reg;
};

} // namespace sandbox
```

`world.cpp` — `spawn` only for this task (tick is Task 2):

```cpp
#include "games/sandbox/world.hpp"

namespace sandbox {

ecs::Entity World::spawn(const Archetype& a, float x, float y) {
    ecs::Entity e = reg.create();
    reg.add<Transform2D>(e, {x, y, 0, 1});
    reg.add<Body>(e, {a.w, a.h});
    reg.add<Sprite>(e, {a.color, a.round});
    if (a.mover)    reg.add<Mover>(e, {a.vx, a.vy});
    if (a.spinner)  reg.add<Spinner>(e, {a.omega});
    if (a.bouncer)  reg.add<Bouncer>(e, {});
    if (a.lifetime) reg.add<Lifetime>(e, {a.ttl});
    if (a.tag)      reg.add<Tag>(e, {a.tag});
    return e;
}

} // namespace sandbox
```

- [ ] **Step 1 — failing test.** Add to `tests/test_sandbox.cpp` (CHECK harness copied from `tests/test_studio.cpp` lines 18–30):

```cpp
#include "games/sandbox/world.hpp"
using namespace sandbox;

static void test_spawn_attaches() {
    World w;
    Archetype a; a.mover = true; a.vx = 5; a.bouncer = true; a.tag = 3;
    ecs::Entity e = w.spawn(a, 10, 20);
    CHECK(w.alive() == 1);
    CHECK(w.reg.has<Transform2D>(e) && w.reg.has<Body>(e) && w.reg.has<Sprite>(e));
    CHECK(w.reg.has<Mover>(e) && w.reg.has<Bouncer>(e) && w.reg.has<Tag>(e));
    CHECK(!w.reg.has<Spinner>(e) && !w.reg.has<Lifetime>(e));
    CHECK(w.reg.get<Transform2D>(e)->x == 10 && w.reg.get<Mover>(e)->vx == 5);
    CHECK(w.reg.get<Tag>(e)->id == 3);
}
int main() { test_spawn_attaches();
    if (g_failures==0) std::printf("sandbox: all tests passed\n");
    else std::printf("sandbox: %d FAILURE(S)\n", g_failures);
    return g_failures; }
```

- [ ] **Step 2 — CMake wiring.** In `CMakeLists.txt`, next to `studio_core`:

```cmake
add_library(sandbox_core STATIC
    src/games/sandbox/world.cpp
    src/games/sandbox/serialize.cpp)
target_include_directories(sandbox_core PUBLIC src)

add_executable(test_sandbox tests/test_sandbox.cpp
    src/games/sandbox/world.cpp
    src/games/sandbox/serialize.cpp)
target_include_directories(test_sandbox PRIVATE src)
add_test(NAME sandbox COMMAND test_sandbox)
```

Create an empty stub `src/games/sandbox/serialize.cpp` with `#include "games/sandbox/serialize.hpp"` and `serialize.hpp` with just `#pragma once` + `#include "games/sandbox/world.hpp"` for now (filled in Task 3) so the lib links. **Actually create serialize.hpp/.cpp minimally here** so CMake builds; Task 3 fills them.

- [ ] **Step 3 — run, expect FAIL** (link error / stub): `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target test_sandbox`. Then implement `world.cpp` `spawn` as above.

- [ ] **Step 4 — run, expect PASS:** `ctest --test-dir build -R sandbox --output-on-failure`.

- [ ] **Step 5 — commit:** `git add -A && git commit -m "sandbox: components + World::spawn"`.

---

## Task 2: `World::tick` systems + command buffer

**Files:** Modify `src/games/sandbox/world.cpp`. Test in `tests/test_sandbox.cpp`.

Implement `tick` with a local command buffer. System order per spec §5:

```cpp
#include <algorithm>
#include <cmath>
// ... in namespace sandbox

namespace {
bool aabb_overlap(const Transform2D& ta, const Body& ba,
                  const Transform2D& tb, const Body& bb) {
    return std::fabs(ta.x - tb.x) * 2 < (ba.w + bb.w) &&
           std::fabs(ta.y - tb.y) * 2 < (ba.h + bb.h);
}
}

void World::tick(float dt) {
    struct SpawnCmd { Archetype proto; float x, y; };
    std::vector<SpawnCmd>    spawns;
    std::vector<ecs::Entity> destroys;

    // 1. spawners
    reg.view<Spawner, Transform2D>([&](ecs::Entity, Spawner& s, Transform2D& t) {
        s.timer += dt;
        while (s.interval > 0 && s.timer >= s.interval) {
            s.timer -= s.interval;
            spawns.push_back({s.proto, t.x, t.y});
        }
    });
    // 2. movers
    reg.view<Mover, Transform2D>([&](ecs::Entity, Mover& m, Transform2D& t) {
        t.x += m.vx * dt; t.y += m.vy * dt;
    });
    // 3. spinners
    reg.view<Spinner, Transform2D>([&](ecs::Entity, Spinner& s, Transform2D& t) {
        t.rot += s.omega * dt;
    });
    // 4. bouncers (need Mover + Body + Transform)
    reg.view<Bouncer, Transform2D>([&](ecs::Entity e, Bouncer&, Transform2D& t) {
        Mover* m = reg.get<Mover>(e); Body* b = reg.get<Body>(e);
        if (!m || !b) return;
        const float hw = b->w * 0.5f, hh = b->h * 0.5f;
        if (t.x - hw < 0)          { t.x = hw; m->vx = std::fabs(m->vx); }
        if (t.x + hw > bounds_w)   { t.x = bounds_w - hw; m->vx = -std::fabs(m->vx); }
        if (t.y - hh < 0)          { t.y = hh; m->vy = std::fabs(m->vy); }
        if (t.y + hh > bounds_h)   { t.y = bounds_h - hh; m->vy = -std::fabs(m->vy); }
    });
    // 5. lifetime
    reg.view<Lifetime>([&](ecs::Entity e, Lifetime& l) {
        l.ttl -= dt;
        if (l.ttl <= 0) destroys.push_back(e);
    });
    // 6. overlaps
    reg.view<OnOverlap, Transform2D, Body>([&](ecs::Entity e, OnOverlap& o, Transform2D& t, Body& b) {
        reg.view<Tag, Transform2D, Body>([&](ecs::Entity ot, Tag& tag, Transform2D& t2, Body& b2) {
            if (tag.id != o.other_tag || ot.index == e.index) return;
            if (!aabb_overlap(t, b, t2, b2)) return;
            switch (o.action) {
                case Action::DestroySelf:  destroys.push_back(e); break;
                case Action::DestroyOther: destroys.push_back(ot); break;
                case Action::SpawnProto:   spawns.push_back({o.proto, t.x, t.y}); break;
            }
        });
    });
    // 7. reap: destroys first (dedup), then spawns
    std::sort(destroys.begin(), destroys.end(),
              [](ecs::Entity a, ecs::Entity c){ return a.index < c.index; });
    destroys.erase(std::unique(destroys.begin(), destroys.end(),
              [](ecs::Entity a, ecs::Entity c){ return a.index == c.index; }), destroys.end());
    for (ecs::Entity e : destroys) reg.destroy(e);
    for (auto& s : spawns) spawn(s.proto, s.x, s.y);
}
```

> Note: nested `view` inside the overlap system is safe — it only READS and pushes to `cmd`; no structural edit happens until reap. `ecs::Entity` has `.index`/`.generation` (see `entity.hpp`).

- [ ] **Step 1 — failing tests** (append to `tests/test_sandbox.cpp` + call in `main`):

```cpp
static void test_mover_integrates_and_deterministic() {
    World w; Archetype a; a.mover = true; a.vx = 10; a.vy = -4;
    ecs::Entity e = w.spawn(a, 100, 100);
    w.tick(0.5f);
    CHECK(approx(w.reg.get<Transform2D>(e)->x, 105.0));
    CHECK(approx(w.reg.get<Transform2D>(e)->y, 98.0));
    World w2; ecs::Entity e2 = w2.spawn(a, 100, 100);
    for (int i=0;i<10;++i){ w.tick(0.016f); w2.tick(0.016f); }
    CHECK(w.reg.get<Transform2D>(e)->x == w2.reg.get<Transform2D>(e2)->x);
}
static void test_spinner() {
    World w; Archetype a; a.spinner = true; a.omega = 2;
    ecs::Entity e = w.spawn(a, 0, 0); w.tick(0.5f);
    CHECK(approx(w.reg.get<Transform2D>(e)->rot, 1.0));
}
static void test_bouncer_reflects() {
    World w; w.bounds_w = 200; w.bounds_h = 200;
    Archetype a; a.mover = true; a.vx = 100; a.vy = 0; a.bouncer = true; a.w = 20; a.h = 20;
    ecs::Entity e = w.spawn(a, 195, 100);   // right edge; hw=10 -> 195+10>200
    w.tick(0.1f);
    CHECK(w.reg.get<Mover>(e)->vx < 0);      // x-vel flipped
    CHECK(w.reg.get<Mover>(e)->vy == 0);     // y untouched
    CHECK(w.reg.get<Transform2D>(e)->x <= 190);
}
static void test_lifetime_despawns() {
    World w; Archetype a; a.lifetime = true; a.ttl = 0.05f;
    w.spawn(a, 0, 0); CHECK(w.alive() == 1);
    w.tick(0.1f);     CHECK(w.alive() == 0);
}
static void test_spawner() {
    World w; Archetype ball; ball.w = 4; ball.h = 4;
    Archetype emitter; emitter.name = "emit"; Spawner sp; sp.interval = 1.0f; sp.proto = ball;
    ecs::Entity e = w.spawn(emitter, 50, 60);
    w.reg.add<Spawner>(e, sp);
    for (int i=0;i<10;++i) w.tick(0.34f);   // 3.4s -> 3 spawns
    CHECK(w.alive() == 4);                    // emitter + 3
}
static void test_overlap_trigger() {
    World w;
    Archetype coin; coin.tag = 1; ecs::Entity c = w.spawn(coin, 100, 100); (void)c;
    Archetype sweeper; OnOverlap o; o.other_tag = 1; o.action = Action::DestroyOther;
    ecs::Entity s = w.spawn(sweeper, 100, 100); w.reg.add<OnOverlap>(s, o);
    w.tick(0.016f);
    CHECK(w.alive() == 1);                    // coin gone, sweeper stays
    CHECK(w.reg.valid(s));
}
```

- [ ] **Step 2 — run, expect FAIL** (tick unimplemented / all zero): `cmake --build build --target test_sandbox && ctest --test-dir build -R sandbox`.
- [ ] **Step 3 — implement `tick`** as above.
- [ ] **Step 4 — run, expect PASS**, then also run under ASan: `cmake --build build-asan --target test_sandbox 2>/dev/null; ./build-asan/test_sandbox` (create `build-asan` if absent, `-DENGINE_SANITIZE=ON`).
- [ ] **Step 5 — commit:** `git commit -am "sandbox: World::tick systems + command buffer (deterministic)"`.

---

## Task 3: Serializer (`to_scene` / `from_scene`) + archetype token codec

**Files:** Fill `src/games/sandbox/serialize.hpp` / `serialize.cpp`. Test in `tests/test_sandbox.cpp`.

**Grammar (pin the proto encoding the spec deferred):** an archetype serializes to a
space-joined token list; a proto is that same list wrapped in `[...]`:

```
archetype-tokens := color=HEX [round] w=F h=F [tag=I]
                    [mover=F,F] [spinner=F] [bouncer] [lifetime=F]
entity-line      := e x=F y=F rot=F <archetype-tokens>
                    [spawner=INTERVAL:[<archetype-tokens>]]
                    [onoverlap=OTHERTAG:ACTION[:[<archetype-tokens>]]]
ACTION           := self | other | spawn
```

`serialize.hpp`:

```cpp
#pragma once
#include <string>
#include "games/sandbox/world.hpp"
namespace sandbox {
std::string to_scene(const World& w);
World       from_scene(const std::string& text);
// exposed for tests + reuse:
std::string archetype_tokens(const Archetype& a);
Archetype   parse_archetype(const std::string& tokens);
}
```

`serialize.cpp` — implement with the same hand-parse style as `src/games/studio/recipe.cpp`
(read that file first for the split/parse helpers). Key points:
- hex color via `%06x` of `color & 0xFFFFFF` (drop alpha; `spawn` re-adds opaque);
  parse with `std::stoul(hex, 0, 16)` → `gfx::rgb`.
- `to_scene`: header `sandbox1\n`, `bounds W H\n`, then one `e ...` line per alive
  entity (iterate `reg` via a `view<Transform2D>` and read optional components with
  `reg.get<>`). Emit tokens in a FIXED order so round-trip text is stable.
- `from_scene`: `World w; parse bounds; for each e-line: parse into an Archetype +
  x/y/rot, w.spawn(...), then if the line had spawner=/onoverlap= add those
  components (they aren't part of Archetype).` Set `Transform2D.rot` after spawn.
- Tolerant: unknown tokens ignored; missing → Archetype defaults (mirror
  `recipe.cpp`'s try/catch-per-value).

- [ ] **Step 1 — failing tests:**

```cpp
#include "games/sandbox/serialize.hpp"

static void test_archetype_codec() {
    Archetype a; a.color = gfx::rgb(0x40,0xcc,0xff); a.round = true;
    a.mover = true; a.vx = 12; a.vy = -3; a.bouncer = true; a.tag = 2; a.w = 18;
    Archetype b = parse_archetype(archetype_tokens(a));
    CHECK(archetype_tokens(a) == archetype_tokens(b));  // stable round-trip
    CHECK(b.mover && b.vx == 12 && b.bouncer && b.tag == 2 && b.round);
}
static void test_scene_roundtrip() {
    World w; w.bounds_w = 800; w.bounds_h = 600;
    Archetype ball; ball.mover=true; ball.vx=40; ball.vy=30; ball.bouncer=true; ball.round=true;
    w.spawn(ball, 100, 120);
    Archetype coin; coin.tag=1; w.spawn(coin, 300, 200);
    const std::string once = to_scene(w);
    CHECK(to_scene(from_scene(once)) == once);          // text round-trip
    // trajectory fidelity: restored world ticks identically
    World a = from_scene(once), b = from_scene(once);
    for (int i=0;i<50;++i){ a.tick(0.016f); b.tick(0.016f); }
    CHECK(to_scene(a) == to_scene(b));
}
static void test_snapshot_restore() {
    World w; Archetype ball; ball.mover=true; ball.vx=40; ball.bouncer=true;
    w.spawn(ball, 50, 50);
    const std::string snap = to_scene(w);
    for (int i=0;i<100;++i) w.tick(0.016f);
    World restored = from_scene(snap);
    CHECK(to_scene(restored) == snap);                  // Stop restores placed state
}
```

- [ ] **Step 2 — run, expect FAIL.**
- [ ] **Step 3 — implement `serialize.cpp`** (read `src/games/studio/recipe.cpp` first for the parse idiom).
- [ ] **Step 4 — run, expect PASS** + ASan clean.
- [ ] **Step 5 — commit:** `git commit -am "sandbox: text serializer (save/load + Play/Stop snapshot)"`.

---

## Task 4: `SandboxScene` — palette / select / inspector / Play-Stop

**Files:** Create `src/games/sandbox/sandbox_scene.hpp` / `sandbox_scene.cpp`. Modify `CMakeLists.txt` (`demo` sources + link `sandbox_core`), `src/main.cpp`.

Use `src/games/studio/studio_scene.cpp` as the structural template (font, `ui_.begin/end`,
`ui::Input` from `ctx.input`, `hovering_ui()` gate, bottom hint line). Key additions:

- State: `World world_; std::vector<Archetype> palette_; int armed_ = -1; ecs::Entity sel_{}; bool has_sel_ = false; bool playing_ = false; std::string snapshot_;` + inspector mirror floats.
- Palette built in ctor (spec §4.1): Ball, Spinner, Emitter, Coin, Sweeper. For
  Emitter/Sweeper, after `world_.spawn` in placement, also `world_.reg.add<Spawner>`
  / `add<OnOverlap>` with the right proto (a helper `place(archetypeIndex, x, y)`).
- **`update(dt,input)`**: `if (playing_) world_.tick(float(dt));` (fixed step; deterministic).
- **`render(ctx)`**:
  1. `world_` draw: `reg.view<Transform2D, Body, Sprite>` → `fill_rect`/`fill_circle`
     (+ `draw_line` from center along `rot` so rotation is visible), `draw_rect`
     outline on the selected entity.
  2. UI pass: PALETTE panel (buttons arm `armed_`), a Play/Stop `button(primary)`,
     INSPECTOR panel when `has_sel_ && !playing_`.
  3. Canvas input (only if `!ui_.hovering_ui()`): on `pressed`, if `armed_>=0` →
     `place(armed_, mx,my)`; else pick topmost entity whose AABB contains (mx,my)
     → select or deselect. On `down` while a selection is held → drag its Transform.
  4. Play/Stop: on toggle to play `snapshot_ = to_scene(world_)`; on toggle to stop
     `world_ = from_scene(snapshot_); has_sel_ = false;`.
  5. `F5` → `assets::write_file("sandbox/scene.scene", ...)`; `F9` → load+`from_scene`.
- Picking helper (2D, no `pick.hpp` needed): point-in-AABB over
  `reg.view<Transform2D, Body>`, keep the last match (topmost = last drawn).

- [ ] **Step 1** — write `sandbox_scene.hpp` (class decl mirroring `studio_scene.hpp`).
- [ ] **Step 2** — write `sandbox_scene.cpp` per the outline above.
- [ ] **Step 3** — `CMakeLists.txt`: add `src/games/sandbox/sandbox_scene.cpp` to the
  `demo` executable sources and `sandbox_core` to its `target_link_libraries`.
- [ ] **Step 4** — `src/main.cpp`: `#include "games/sandbox/sandbox_scene.hpp"` and a
  `--sandbox` branch (960×600, smooth, highdpi, `supersample = kAA`) mirroring `--studio`.
- [ ] **Step 5** — build the whole `demo`: `cmake --build build`. Expect zero warnings.
- [ ] **Step 6** — full test sweep: `ctest --test-dir build` (expect all suites incl.
  `sandbox` green).
- [ ] **Step 7** — commit: `git commit -am "sandbox: --sandbox scene (palette/drag/inspector/Play-Stop)"`.

> Manual acceptance (GUI, human): `./build/demo --sandbox` — place a Ball, Play (it
> bounces), Stop (returns to placed spot), place a Coin + Sweeper overlapping, Play
> (coin vanishes), F5/F9.

---

## Task 5: Docs + README + merge

**Files:** Create guidebook chapter(s) `docs/book/76-declarative-sandbox.md` (ECS
behaviors as data, the tick order + command buffer, Play/Stop-as-snapshot, the
serializer). Modify `README.md` (`--sandbox` run line + roadmap row).

- [ ] **Step 1** — write `docs/book/76-declarative-sandbox.md` (match the depth/shape
  of ch.73–75: concept, code walkthrough, ASCII diagram of the tick pipeline, a
  worked overlap example, pitfalls incl. the mid-`view` structural-edit trap,
  glossary, exercises).
- [ ] **Step 2** — `README.md`: add `--sandbox` to the run list and a "Sandbox v1" roadmap row.
- [ ] **Step 3** — commit: `git commit -am "sandbox: guidebook ch.76 + README"`.
- [ ] **Step 4** — final sweep `ctest --test-dir build` (all green), then merge:
  `git checkout main && git merge --no-ff feat/sandbox-editor -m "Merge Sandbox v1: declarative 2D playground" && git branch -d feat/sandbox-editor`.

---

## Self-Review

**Spec coverage:** §3 architecture → Tasks 1–4. §4 components → Task 1. §5 tick order
→ Task 2. §6 serializer → Task 3. §7 scene interactions → Task 4. §8 all 9 tests →
Tasks 1–3 (spawn, mover+determinism, spinner, bouncer, lifetime, spawner, overlap,
round-trip, snapshot). §9 deferrals are out of scope by design. §10 command-buffer
risk → Task 2 + determinism tests.

**Placeholder scan:** proto grammar pinned in Task 3; every code step has real code;
test assertions concrete. Task 4 is prose-guided (GUI, matches studio template) but
lists exact state, methods, and files — no invented APIs.

**Type consistency:** `Archetype` fields identical across Tasks 1/3/4; `Action` enum
values (`DestroySelf/DestroyOther/SpawnProto`) consistent; `World::spawn` signature
`(const Archetype&, float, float)` used everywhere; `to_scene/from_scene` names stable.
`ecs::Entity` accessed via `.index` (matches `entity.hpp`), `reg.has/get/add/valid`
match `registry.hpp`.
