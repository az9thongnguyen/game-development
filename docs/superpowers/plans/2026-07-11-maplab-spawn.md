# Map Lab Author-Placed Spawn Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or executing-plans.

**Goal:** Levels carry a player start (cell + facing); the Lab places it, the raycaster reads it, replacing the hard-coded spawn.

**Architecture:** `fpsmap1` grows one optional `spawn` line (backward compatible). Raycaster overrides its default from the map in the ctor body. Map Lab adds a Spawn tool + facing + marker.

**Tech Stack:** C++20, existing fps/maplab; no new libs.

---

### Task 1: Format + raycaster (TDD)

**Files:** Modify `fps/map.{hpp,cpp}`, `fps/raycast_scene.cpp`, `tests/test_fps.cpp`.

- [ ] Extend `test_map_serialize`: spawn-less → `spawn_cx==-1`; set spawn emits `spawn 2 1` + round-trips; out-of-range `spawn 9 9` rejected, grid still loads.
- [ ] `Map`: `int spawn_cx=-1, spawn_cy=-1; float spawn_dir=0`. `to_text` emits `spawn` only when set; `from_text` reads/validates optional trailing line; `default_map` sets (3,8,0).
- [ ] Raycaster ctor body: if spawn set, override posX/posY (cell centre), dir=cos/sin, plane=(-dirY,dirX)·0.66.
- [ ] Build `test_fps`; `ctest -R '^fps$'` PASS.

### Task 2: Map Lab Spawn tool

**Files:** Modify `maplab/maplab_scene.{hpp,cpp}`.

- [ ] `int tool_` (Paint/Fill/Spawn) replaces `fill_mode_`; `int facing_`.
- [ ] Tool cycle button; Facing button in Spawn mode (E/S/W/N → idx·π/2).
- [ ] Canvas: Spawn click sets `spawn_cx/cy/dir`; Fill floods; Paint drags.
- [ ] Draw a spawn dot + facing tick on the grid; update caption.
- [ ] Build demo; manual `--maplab` (place spawn, Save) → `--fps` (starts there).

### Task 3: Asset + docs + checkpoint

- [ ] Re-seed `assets/maps/level_00.map` via the codec with `spawn 3 8 0`.
- [ ] `docs/book/89-maplab-spawn.md`; README `--maplab` note.
- [ ] Full `ctest` green; `--no-ff` merge; memory checkpoint; **then push all (authorized).**
