# Map Lab Author-Placed Spawn — design

**Track B (mini studio). Date: 2026-07-11.**

## Goal

Let a level own its **player start** (cell + facing) instead of the raycaster
hard-coding it. Fixes a real trap: an authored level whose layout doesn't match the
hard-coded (3.5, 8.5) spawns the player in a wall.

## Design

### Format (backward compatible)

`fps::Map` gains `int spawn_cx=-1, spawn_cy=-1; float spawn_dir=0` (`spawn_cx<0` =
unset). `fpsmap1` gains an **optional** trailing line `spawn CX CY DIR`:
- `to_text` emits it only when set → old files stay byte-identical.
- `from_text` reads it if present, validates against grid bounds (out-of-range →
  ignored, grid still loads), leaves the sentinel otherwise → old files parse.
- `default_map()` sets `spawn (3,8,0)` (the legacy start).

### Raycaster

Keep the default in the member-init list; in the ctor body, if the map defines a
spawn, override `posX_/posY_` (cell centre), `dirX_/dirY_ = cos/sin(dir)`, and the
camera plane `(-dirY_, dirX_)·0.66` (⟂ to dir, FOV 0.66). Unset → unchanged default.

### Map Lab

Paint/Fill toggle becomes a 3-way **Tool** cycle (Paint / Fill / Spawn). Spawn mode:
click sets the start cell; a **Facing** button cycles E/S/W/N (`idx·π/2`, map +y =
south). A green dot + facing tick marks the spawn on the grid. Save writes it via
`to_text`.

### Asset

Re-seed `assets/maps/level_00.map` through the codec with `spawn 3 8 0` so the level
is self-contained and `--fps` reads an authored start out of the box.

## Testing

`tests/test_fps.cpp` (`test_map_serialize`): spawn-less map → `spawn_cx==-1`; a set
spawn emits the `spawn 2 1` token and round-trips `to_text(from_text)==` exactly;
out-of-range `spawn 9 9` rejected while the grid loads.

## Deferrals (ponytail)

Sub-cell / free-angle spawn (format already stores a float dir — UI only); multiple
spawns / spawn types (player vs enemy vs item); the raycaster needs one player start.
