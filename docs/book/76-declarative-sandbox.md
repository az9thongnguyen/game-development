# Chapter 76 — The Declarative Sandbox

> Code: `src/games/sandbox/world.{hpp,cpp}`, `serialize.{hpp,cpp}`,
> `sandbox_scene.{hpp,cpp}`; tests `tests/test_sandbox.cpp`; run `./build/demo --sandbox`.

The Texture Lab (ch.75) let you *author an asset*. The sandbox lets you *author a
little world*: drop actors onto a canvas, give them behavior, and press **Play** to
watch the behavior run. The interesting question is **how you express behavior
without writing a program**. Our answer is the lowest rung that still works:
behavior is **data attached to entities**, not a script. This chapter is about why
that rung is enough, and the three ideas that make it hold together — a
data-driven ECS, a deterministic `tick` with a command buffer, and "Play/Stop as a
snapshot."

## 76.1 Behavior as data

A traditional engine hangs behavior off code: a `Ball` class with an `update()`
method. Add a bouncing crate and you write a `Crate` class; add a bouncing crate
that also spins and you copy-paste. The combinatorial explosion is the problem
inheritance was supposed to solve and never did.

The ECS flips it. An **entity** is just an id. A **component** is a plain struct of
data. A **system** is a free function that walks every entity holding a particular
set of components and does one small thing. "A bouncing, spinning crate" is not a
class — it is an entity that happens to carry `Transform2D + Body + Sprite + Mover
+ Bouncer + Spinner`. You compose behavior by *attaching data*, the same way you'd
tick checkboxes in an editor:

```cpp
struct Transform2D { float x, y, rot, scale; };   // where/how it sits
struct Mover       { float vx, vy; };             // "I drift"
struct Spinner     { float omega; };              // "I rotate"
struct Bouncer     {};                            // "I reflect at the walls"
struct Lifetime    { float ttl; };                // "I expire"
```

`Bouncer` has **no fields** — it is a pure *tag*. Its mere presence is the
instruction. That is the whole trick of declarative behavior: the set of
components an entity carries *is* its program.

`World::spawn` is the single funnel that turns a template (`Archetype`) into an
entity, adding exactly the flagged components:

```cpp
ecs::Entity World::spawn(const Archetype& a, float x, float y) {
    ecs::Entity e = reg.create();
    reg.add<Transform2D>(e, {x, y, 0, 1});
    reg.add<Body>(e, {a.w, a.h});
    reg.add<Sprite>(e, {a.color, a.round});
    if (a.mover)   reg.add<Mover>(e, {a.vx, a.vy});
    if (a.spinner) reg.add<Spinner>(e, {a.omega});
    if (a.bouncer) reg.add<Bouncer>(e, {});
    // ...
}
```

Everything downstream — the palette, spawners, triggers, the file loader — goes
through this one funnel. One place decides how a template becomes an entity.

## 76.2 The tick pipeline

`World::tick(dt)` is the heartbeat. Each system is a `reg.view<...>` over the
entities that have the components it cares about:

```
       ┌─────────────┐   entities with…
  1.   │  spawners   │   Spawner + Transform2D   → queue new entities
  2.   │  movers     │   Mover   + Transform2D   → pos += v·dt
  3.   │  spinners   │   Spinner + Transform2D   → rot += ω·dt
  4.   │  bouncers   │   Bouncer + Transform2D   → clamp + flip velocity at bounds
  5.   │  lifetime   │   Lifetime                → ttl -= dt; queue dead
  6.   │  overlaps   │   OnOverlap + T + Body     → queue action on first Tag hit
       └──────┬──────┘
              ▼
  7.   ┌─────────────┐
       │    reap     │   apply queued destroys (deduped), then queued spawns
       └─────────────┘
```

The **order is fixed and meaningful**. Movers run before bouncers so a ball that
stepped through a wall this frame is pushed back the same frame. Lifetime and
overlaps only *mark* work; nothing is created or destroyed until step 7. That last
point is not a style choice — it is a correctness requirement, and §76.3 is why.

Worked example — a `Mover{vx:10, vy:-4}` at `(100,100)`, one `tick(0.5)`:

```
x' = 100 + 10·0.5 = 105
y' = 100 + (-4)·0.5 = 98
```

No randomness, no wall-clock: `tick` reads only its `dt` and the current
components. Same world + same `dt` sequence ⇒ bit-identical result. That
determinism is what makes Play/Stop and the tests trustworthy.

## 76.3 The command buffer (don't edit the pool you're walking)

`ecs::Registry::view` hands your callback **references into the component pools**.
If a system destroys an entity or adds a component *of a type being iterated* mid-
walk, the pool can reallocate or swap-and-pop, and those references dangle — a
classic iterator-invalidation bug (`registry.hpp` documents the precondition).

The fix is a **command buffer**: systems never mutate structure directly, they
*record intent* into local vectors, and a single reap phase applies it after all
`view`s have finished:

```cpp
std::vector<SpawnCmd>    spawns;
std::vector<ecs::Entity> destroys;
// ... systems push into spawns / destroys ...
// step 7:
std::sort(destroys...); destroys.erase(std::unique(...));   // dedup: two triggers
for (ecs::Entity e : destroys) reg.destroy(e);              // may name the same victim
for (auto& s : spawns)         spawn(s.proto, s.x, s.y);
```

Two subtleties worth their lines of code:

- **Dedup destroys.** A coin can sit under two sweepers; both queue "destroy the
  coin." Destroying a stale handle twice is a bug, so we sort-by-index and `unique`
  before reaping.
- **Destroys before spawns.** Reaping deaths first keeps entity counts tight and
  means a spawner and a despawn in the same tick don't fight over ids.

The overlap system even nests a `view` inside a `view` (for each `OnOverlap`, scan
every `Tag`). That is fine **because the inner loop only reads and records** — the
O(n²) scan touches no structure. (`// ponytail: O(n^2) is fine for a sandbox;
grid-hash if counts grow.`)

## 76.4 Event → action, still declarative

Behaviors so far are autonomous ("I drift", "I spin"). Real games need
*reactions*: a coin vanishes when a sweeper touches it. That is the `OnOverlap`
component — one declarative rule, no scripting language:

```cpp
struct OnOverlap { int other_tag; Action action; Archetype proto; };
enum class Action { DestroySelf, DestroyOther, SpawnProto };
```

Read it in English: *"when my AABB overlaps any entity tagged `other_tag`, do
`action`."* A sweeper is `OnOverlap{other_tag:1, DestroyOther}`; a coin is
`Tag{1}`. Overlap uses centered-AABB math:

```cpp
bool overlap = |ta.x - tb.x|·2 < (ba.w + bb.w)      // overlap on x…
            && |ta.y - tb.y|·2 < (ba.h + bb.h);     // …and on y
```

This single primitive already expresses pickups, projectiles that destroy
themselves on hit (`DestroySelf`), and spawners triggered by contact
(`SpawnProto`). It is the seed of a rule system; ch. (future) can grow it to
`OnTimer`/`OnKey` and multiple rules per entity. Crucially, we did **not** reach
for an embedded interpreter — the declarative rung carries this weight.

## 76.5 Play/Stop is a snapshot, and a snapshot is a save file

The editor needs "run the simulation, then put everything back exactly where I
placed it." The lazy, correct implementation reuses the thing we already need for
save/load: **serialization**.

```cpp
void toggle_play() {
    if (!playing_) { snapshot_ = to_scene(world_); playing_ = true; }  // freeze
    else           { world_ = from_scene(snapshot_); playing_ = false; } // thaw
}
```

`to_scene` walks the world and writes one tolerant text line per entity;
`from_scene` parses it back through the same `spawn` funnel. That one pair of
functions serves **three** jobs — F5 save, F9 load, and Play/Stop — because they
are all the same operation: *turn the world into text, turn text into the world.*
No separate "snapshot" mechanism exists, and that absence is the design.

The format is deliberately boring (compare `iso/serialize.cpp`, `studio/recipe.cpp`):

```
sandbox1
bounds 960 600
e x=100 y=120 rot=0 color=f0c846 round w=26 h=26 mover=120,90 bouncer
e x=60 y=60 rot=0 color=50a0f0 w=22 h=22 spawner=0.6:[color=b4d2ff round w=8 h=8 mover=0,140 lifetime=2.5]
```

Two parsing details earn their keep:

- **Bracket-aware tokenization.** A `spawner`'s proto is a whole archetype nested
  in `[...]`, and it contains spaces. The tokenizer splits on spaces only at
  bracket-depth 0, so the proto survives as one token.
- **Tolerance.** Unknown keys are ignored; missing keys fall back to `Archetype`
  defaults. Old files load into newer builds without a migration step.

Round-trip is the contract the tests pin down: `to_scene(from_scene(x)) == x`, and
— stronger — a restored world *ticks identically* to the original. If snapshots
weren't faithful, Play/Stop would quietly corrupt your scene.

## 76.6 Where the simulation runs

`scene.hpp` splits `update(dt)` (fixed-step logic, called 0..n times per frame)
from `render(ctx)` (draw once). The sandbox honors it precisely:

- **`update`** advances the sim — and *only* when playing:
  `if (playing_) world_.tick(float(dt));`. Because `App` calls `update` at a fixed
  1/60 s, `Spawner.timer` and `Lifetime.ttl` advance in exact 1/60 increments —
  the determinism §76.2 promised.
- **`render`** does everything interactive: draw the world, run the immediate-mode
  palette/inspector, and handle canvas clicks (place/select/drag). Editing is not
  timing-sensitive, so it lives with drawing, studio-style.

The `ui_.hovering_ui()` gate keeps a click on a palette button from *also* placing
an actor on the canvas underneath it — the same guard the Texture Lab relies on.

## Pitfalls

- **Structural edits inside `view`.** The #1 ECS trap. Route every destroy/spawn
  through the command buffer; never call `reg.destroy`/`reg.add<IteratedType>`
  from inside the callback. (§76.3)
- **Comparing worlds with different histories.** A determinism test must tick two
  worlds *identically*. Our first draft compared a world that had an extra warm-up
  tick against a fresh one and "failed" — the code was right, the test lied.
- **Serializing transient sim state.** We deliberately omit `Spawner.timer` (it is
  0 at Play time and resets on load). Serializing mid-flight timers would make
  "identical placement" files differ byte-for-byte.
- **Float text stability.** `%g` is chosen so `emit(parse(emit(x))) == emit(x)`
  for our value ranges; a fixed `%f` with trailing zeros would round-trip fine too,
  but `%g` keeps the files readable.

## Glossary

- **Entity** — an integer handle (index + generation); owns no data itself.
- **Component** — a plain-data struct attached to an entity.
- **Tag component** — a fieldless component whose *presence* is the signal
  (`Bouncer`).
- **System** — a function over all entities matching a component set (`view<...>`).
- **Archetype** — a flat template describing which components to attach; the unit
  the palette, spawners, triggers, and the loader all share.
- **Command buffer** — deferred structural edits, applied after iteration.
- **Snapshot** — the serialized world; doubles as the Play/Stop restore point.

## Exercises

1. **A new behavior in ~10 lines.** Add `struct Wander { float rate; };` and a
   system that nudges a `Mover`'s velocity by a small deterministic function of the
   entity index each tick (no RNG — keep it reproducible). Hint: add it between
   movers and bouncers; you only touch `world.cpp` and one palette entry.
2. **`OnOverlap{SpawnProto}` in play.** Give the Sweeper `SpawnProto` instead of
   `DestroyOther` and watch it emit pellets on contact. What breaks if the proto
   itself carries a `Tag` the sweeper reacts to? (Answer: a feedback loop — a good
   argument for keeping protos *flat*, §76.1.)
3. **A second rule.** `OnOverlap` allows one rule per entity. Change it to
   `std::vector<Rule>` and update `to_scene`/`from_scene`. Which of the three uses
   of the serializer must you re-test? (All of them — that's the point of one
   format, three jobs.)
4. **Textured actors.** Wire a Texture Lab `.hrt` into `Sprite` so an actor draws a
   generated texture instead of a solid color. This is where the two Mini-Studio
   modules meet — the v2 hook mentioned in the design spec.
