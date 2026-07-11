# Chapter 78 — The Map / Level Lab: Author Once, Play Elsewhere

> Code: `src/games/maplab/edit.{hpp,cpp}` (`maplab_core`),
> `src/games/maplab/maplab_scene.{hpp,cpp}`, `src/games/fps/map.{hpp,cpp}`
> (`to_text`/`from_text`), `src/games/fps/raycast_scene.cpp` (loads the level);
> tests `tests/test_maplab.cpp`, `tests/test_fps.cpp`; run `./build/demo --maplab`
> then `./build/demo --fps`.

The Texture Lab (ch.73–75) authored a *pixel* asset. This chapter's tool authors a
*structural* one: a **level**. You paint a grid of walls and floor in `--maplab`, press
Save, launch `--fps`, and walk through the level you just drew. Two programs, one asset,
one format — the same produce→consume shape as textured sprites (ch.77), now for the thing
a game is built *around* rather than painted *with*.

The interesting decisions here are not in the editor UI. They are three questions about
*data*: what model to edit, where the edit logic lives, and how two programs agree on a
file. Get those right and the editor is a thin skin over them.

## 1. Don't invent a model — the game already has one

The raycaster's world (ch. on the FPS) is `fps::Map`:

```cpp
struct Map { int w, h; std::vector<uint8_t> cells; };  // row-major; 0 = floor, >0 = wall id
```

That is *already* a generic tile grid. The lazy, correct move is to make the Lab author
**exactly this type** — no `maplab::Grid`, no conversion step, no risk of the two drifting
apart. The editor edits a `fps::Map`; the serializer serializes a `fps::Map`; the raycaster
consumes a `fps::Map`. One model end to end.

This is the same instinct as ch.77's "a texture is a name": reach for the representation
that already exists before minting a new one. A parallel model is a second source of truth,
and second sources of truth are where bugs breed.

(iso has its *own* grid — `Terrain` enum, different semantics — so the Lab does **not** feed
iso in v1. Bridging them needs a cell-id → terrain mapping; that is a separate slice, not a
free lunch. Naming that boundary is part of the design, not a gap in it.)

## 2. Dense grid, and the edit ops worth testing

A level's floor is **dense**: every cell has exactly one value, always. That is why it is a
flat `std::vector<uint8_t>` and not an entity list (ch.28's dense-vs-sparse lesson, applied
again). Editing a dense grid is three operations, and they live in `maplab_core` — pure, no
UI, no files, so they unit-test with no window:

- `set_cell` — one bounds-checked write. Trivial, but every other op routes through it, so
  its bounds check is the *only* place off-grid coordinates are handled.
- `fill_rect` — a clamped inclusive rectangle. Used by `bordered()` to lay four wall strips.
- `flood_fill` — the one with real logic, and the one that earns the test file.

### Flood fill: the details that bite

```cpp
void flood_fill(fps::Map& m, int x, int y, uint8_t id) {
    if (!in(m, x, y)) return;
    const uint8_t from = cell(m, x, y);
    if (from == id) return;                         // (A) no-op guard
    std::vector<std::pair<int,int>> stack{{x, y}};
    while (!stack.empty()) {
        auto [cx, cy] = stack.back(); stack.pop_back();
        if (!in(m, cx, cy) || cell(m, cx, cy) != from) continue;  // (B) re-check on pop
        cell(m, cx, cy) = id;
        stack.push_back({cx+1, cy}); stack.push_back({cx-1, cy});
        stack.push_back({cx, cy+1}); stack.push_back({cx, cy-1});
    }
}
```

Three things make it correct:

- **(A) The no-op guard.** Filling a region with the colour it already has would loop
  forever — every neighbour still equals `from`, so nothing ever stops matching. Returning
  early is not an optimisation; it is what keeps the loop finite. `test_maplab` asserts a
  same-id fill terminates.
- **(B) Re-check on pop, not on push.** A cell can be pushed by several neighbours before it
  is processed. Checking `== from` again *when we pop it* means the second visit sees it has
  already become `id` and skips it. Without that, a cell gets rewritten (harmless here) and,
  worse, re-expands (not harmless).
- **Iterative, not recursive.** A 24×16 grid would survive recursion, but a real level would
  overflow the call stack. An explicit `std::vector` stack costs one line and removes the
  ceiling. `// ponytail` would call this the upgrade you take *now* because it is free.

The test that matters most is **confinement**: build a bordered grid, drop a wall across the
middle, flood one side, and assert the *other* side is untouched. That is the whole promise
of a flood fill — it respects walls — and it is one `CHECK` away.

## 3. One format, both sides (`fpsmap1`)

If the editor wrote one format and the raycaster read another, they would drift the first
time either changed. So there is exactly one (de)serializer, and it lives in `fps_core` where
*both* sides can reach it:

```
fpsmap1
size 16 16
row 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1
row 1 0 0 0 0 0 0 0 2 0 2 2 2 2 0 1
...
```

`to_text` and `from_text` are **pure** — string↔`Map`, no I/O — for two reasons. First,
they unit-test with no assets (`test_fps` round-trips a hand-built map and feeds `from_text`
four malformed strings, each of which must return `nullopt`). Second, purity keeps `fps_core`
free of the asset seam: the *reading of bytes* happens in the demo-side scenes
(`maplab_scene` saves, `raycast_scene` loads), never in the core library. This is the exact
layering ch.77 used for images — the capable-but-heavy layer stays at the edge; the pure core
stays testable.

`from_text` **fails closed.** A bad header, a `size` that doesn't match the row count, a cell
out of `[0,255]` — any of these yields `nullopt`, never a half-built grid. The consumer then
does the safe thing:

```cpp
Map load_level() {
    if (auto bytes = assets::load_file("maps/level_00.map"))
        if (auto m = from_text(std::string(bytes->begin(), bytes->end()))) return *m;
    return default_map();                            // absent or malformed -> the built-in
}
```

Author a level and it loads; delete it and the game still runs on its hand-built default.
A missing asset is a fallback, not a crash.

## 4. The join, and where the seams are

```
   --maplab  ── edits ──►  fps::Map  ── to_text ──►  maps/level_00.map  (assets:: write)
                              ▲                              │
                          maplab_core                        │  assets:: read + from_text
                       (set/fill/flood)                      ▼
                                                       --fps  RaycastScene(load_level())
```

Both ends point at the same `assets` base (`main.cpp` calls `set_base_path("assets")` once),
so `maps/level_00.map` written by the Lab is the same file the raycaster reads. Nothing in
`fps_core` or `maplab_core` knows about files; nothing in the pure serializer knows about the
editor. Each seam is crossed by exactly one small function, and every layer below the scene
is headless-testable. That is why a whole new tool landed as four short files plus a handful
of edits.

## Pitfalls

- **A second grid type.** The moment `maplab::Grid` exists, it and `fps::Map` disagree.
  Edit the game's own type.
- **Flood fill without the no-op guard.** Infinite loop. `from == id → return`.
- **Putting `assets::` in `fps_core`.** `test_fps` would need the asset seam linked in and the
  library stops being pure. Keep byte I/O in the scenes.
- **A loaded level trapping the player.** The raycaster's spawn `(3.5, 8.5)` is fixed; a Lab
  map with a wall there stands the player inside geometry. The seeded `level_00` keeps the
  spawn (and every sprite cell) open. A proper fix — an author-placed spawn cell — is a v2
  deferral, and a real one.

## Glossary

- **`fpsmap1`** — the level text format: header, `size W H`, then `H` `row` lines of `W`
  integers. Emitted by `to_text`, parsed (fail-closed) by `from_text`.
- **dense grid** — every cell always has a value; stored as a flat `vector`, not an entity
  set. The right shape for "the floor".
- **flood fill** — replace a 4-connected region of one id with another; the editor's "fill
  bucket".
- **fail closed** — on bad input, return "nothing" (`nullopt`) rather than a partial result.

## Exercises

1. **Author-placed spawn.** Add a `start_x/start_y` to the format and a "Spawn" palette tool;
   have the raycaster read them instead of the hard-coded `(3.5, 8.5)`. What does `from_text`
   do with an old file that lacks them?
2. **Wall textures.** Cell ids already select a wall type. Map each id to a Texture Lab
   `.hrt` so a Lab-authored level renders with Lab-authored textures — the join of both tools.
   Which layer holds the id→texture table?
3. **Resize.** Add a resize that preserves the overlapping region. Is it a `maplab_core` op or
   a scene action? Write its test first.
4. **iso bridge.** Define a cell-id → `iso::Terrain` mapping and load a `fpsmap1` level into the
   farm sim. Where does it break down, and what does that tell you about "one model for all
   games"?
