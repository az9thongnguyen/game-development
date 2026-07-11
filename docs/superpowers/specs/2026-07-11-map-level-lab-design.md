# Map / Level Lab — Design Spec

**Date:** 2026-07-11
**Track:** B (Mini Studio) — sub-project 4. The second *authoring* tool (after Texture Lab),
producing levels the **fps** raycaster consumes.
**Status:** approved (standing auto-approve), implementing.

## 1. Goal

A Mini-Studio tool (`--maplab`) to author a **dense tile grid** — paint walls/floor with a
brush, flood-fill regions, pick cell types from a palette — and **Save** it as a `.map`
asset. The **fps** raycaster loads that asset on startup instead of its hard-coded level.
Same shape as the Textured Sprites join: a Lab *produces* an asset, a game *consumes* it by
a known path, with **one shared format** on both sides.

## 2. The key decision: reuse `fps::Map`, don't invent a model

`fps::Map { int w, h; std::vector<uint8_t> cells; }` (row-major, `0` = empty/floor, `>0` =
wall id) is *already* a generic tile grid. The Lab authors exactly this type. No parallel
model, no conversion layer — the editor, the serializer, and the raycaster all speak
`fps::Map`. (iso's `Terrain` grid is a *different* semantic — deferred, see §9.)

## 3. Where each piece lives (dependency hygiene)

| Piece | File | Layer | Why there |
|---|---|---|---|
| `to_text` / `from_text` (pure string↔Map) | `fps/map.cpp` (`fps_core`) | pure, no IO | shared by editor + raycaster; unit-testable with no assets |
| `flood_fill` / `fill_rect` / `set_cell` (edit ops) | `maplab/edit.{hpp,cpp}` (`maplab_core`) | pure, no IO | the interesting logic, headless-testable |
| Save/Load (`assets::` read/write) | `maplab_scene.cpp` + `raycast_scene.cpp` | demo side (SDL/IO) | keeps every `*_core` and its test IO-free |

This mirrors the constraint the Textured Sprites slice honoured: the core names/edits data;
only the scene layer touches the asset seam.

## 4. Text format (`fpsmap1`)

Human-readable, tolerant, line-based:

```
fpsmap1
size 24 16
row 1 1 1 1 1 ... (24 ints)
row 1 0 0 0 0 ... (24 ints)
...                (16 row lines)
```

- Header line `fpsmap1`; `size W H`; then `H` lines each starting `row` followed by `W`
  integers (0..255).
- `from_text` returns `std::optional<Map>`: `nullopt` if the header is wrong or the row/cell
  counts don't match `size` (fail closed — never hand back a half-parsed grid).
- `to_text(from_text(s)) == s` for any grid the editor produces (round-trip invariant).

## 5. Edit operations (`maplab_core`)

Pure functions on `fps::Map` (all bounds-safe, no-op off-grid):

- `set_cell(Map&, x, y, uint8_t id)`.
- `fill_rect(Map&, x0, y0, x1, y1, uint8_t id)` — inclusive, clamped.
- `flood_fill(Map&, x, y, uint8_t id)` — 4-connected; replaces the contiguous region of the
  cell's *current* id with `id`; no-op if `id` already equals it (prevents an infinite/no-op
  scan). Iterative stack (no recursion — a 24×16 grid is small but a big level shouldn't
  blow the stack).

## 6. Editor scene (`--maplab`)

State: `fps::Map map_` (new = a bordered 24×16 grid), `uint8_t brush_ = 1`, `enum Tool { Paint,
Fill }`, a fixed palette of ids 0..3, a save counter + in-session collection (mirrors Texture
Lab).

- **Canvas:** the grid drawn as filled cells sized to fill the canvas (cell px = fit to
  window minus the palette gutter), thin grid lines, a highlight on the hovered cell.
- **Interaction:** left-drag paints `brush_` (Paint) or the mouse-press flood-fills (Fill) at
  the hovered cell. Gated by `!ui_.hovering_ui()`.
- **Palette panel:** id swatches Floor(0)/Wall(1)/Room(2)/Pillar(3) → sets `brush_`; a
  Paint/Fill toggle; **New** (reset to bordered grid); **Save** (writes `maps/level_NN.map`);
  a collection list of saved names → Load.
- No Play/Stop — this is a pure editor (unlike the sandbox).

## 7. fps consumes the asset

`RaycastScene` ctor changes from `map_(default_map())` to: load `maps/level_00.map` via the
asset seam + `fps::from_text`; fall back to `default_map()` when absent/malformed. One line
plus a small helper. So: author in `--maplab`, Save, launch `--fps`, walk your level.

## 8. Seeding

Write a sample `assets/maps/level_00.map` (a small room-and-corridors level, authored as a
`fps::Map` in a headless step) so `--fps` shows a Lab level and `--maplab` opens with content
on a fresh checkout — same courtesy as the seeded textures.

## 9. Non-goals (deferred)

- **iso consumption** — needs a cell-id → `Terrain` mapping and iso's object layer; a separate
  slice. v1 targets fps only (its model *is* the grid).
- **Resize / arbitrary sizes** — v1 authors at a fixed 24×16 (or the loaded size). Preset
  sizes / a resize handle later.
- **Per-cell textures** (wall id → Texture Lab `.hrt`) — the natural *next* join, deferred.
- **Undo/redo, multi-map tabs, entity/spawn placement.**

## 10. Tests

- **fps serializer** (`tests/test_fps.cpp`, no assets): `to_text`→`from_text` round-trips a
  hand-built map; a garbage string and a size/row mismatch both yield `nullopt`; the emitted
  text round-trips (`to_text(from_text(x)) == x`).
- **maplab edit** (`tests/test_maplab.cpp`, `maplab_core`): `flood_fill` recolours a bounded
  region and stops at a wall border (a cell outside the region keeps its id); `flood_fill`
  with the same id is a no-op; `fill_rect` clamps; `set_cell` is bounds-safe.

## 11. Files

- Modify: `src/games/fps/map.hpp` + `map.cpp` (`to_text`/`from_text`), `tests/test_fps.cpp`
- Modify: `src/games/fps/raycast_scene.cpp` (load Lab map, fallback)
- Create: `src/games/maplab/edit.{hpp,cpp}`, `src/games/maplab/maplab_scene.{hpp,cpp}`,
  `tests/test_maplab.cpp`
- Modify: `CMakeLists.txt` (`maplab_core`, `test_maplab`, `maplab_scene.cpp`→demo, assets→test_fps),
  `src/main.cpp` (`--maplab`)
- Seed: `assets/maps/level_00.map`
- Docs: guidebook `docs/book/78-map-level-lab.md`, README roadmap row

## 12. Risks

- **fps_core gaining an assets dep.** Avoided: the pure `to_text`/`from_text` stay in
  `fps_core`; the `assets::` calls live only in the demo-side scenes. `test_fps` needs no
  assets; if it ever does, compile `assets.cpp` directly (the codebase's documented pattern).
- **Format drift** between editor and raycaster. Avoided by a single shared `from_text`.
- **Big flood fill.** Iterative stack, not recursion.
