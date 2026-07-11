# Sprite Animation (Flipbook) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or executing-plans.

**Goal:** A pure `anim::Flipbook` frame-index player + a vertical sprite sheet + an `--anim` demo — sprite-sheet animation with no new renderer code.

**Architecture:** `flipbook.{hpp,cpp}` joins the `anim`/`tween_core` lib. Sheet packed vertically so each frame is a contiguous `gfx::Sprite` drawn by the existing `blit_scaled`. Demo reuses `ui_core` + `image` loading.

**Tech Stack:** C++20, no SDL in the core, no new deps.

---

### Task 1: Flipbook core (TDD)

**Files:** Create `src/engine/anim/flipbook.{hpp,cpp}`, `tests/test_flipbook.cpp`; Modify `CMakeLists.txt` (flipbook.cpp → `tween_core`; `test_flipbook`).

- [ ] Write `tests/test_flipbook.cpp`: single-frame; 8@8fps step + wrap; in-range + bounded-t over long run; one-shot advance/hold/done; reset.
- [ ] Write `flipbook.hpp` (`struct Flipbook`).
- [ ] Write `flipbook.cpp` (`update` wraps t when looping; `frame()` mod/clamp; `done()`).
- [ ] CMake: add flipbook.cpp to `tween_core`; add `test_flipbook`. Build; `ctest -R '^flipbook$'` PASS; ASan/UBSan PASS.

### Task 2: Sprite-sheet asset

**Files:** Create `assets/sprites/spin_8.hrt` (generated headlessly).

- [ ] Write a throwaway generator: 48×384 vertical sheet, 8 frames, a rotating spinner with a fading trail; `encode_hrt` → write bytes.
- [ ] Compile with `image.cpp`+`assets.cpp`, run, verify header (`xxd`: HRT1 + 48×384).

### Task 3: `--anim` scene + wiring

**Files:** Create `src/games/anim/anim_scene.{hpp,cpp}`; Modify `src/main.cpp`, `CMakeLists.txt`.

- [ ] Scene: load `sprites/spin_8.hrt`; `frame_sprite(f)` = contiguous view; render big frame + strip + active outline; fps/loop/playing/restart UI; frame counter; "asset missing" fallback.
- [ ] main.cpp `--anim` dispatch + include; CMake add scene source (demo already links `tween_core`).
- [ ] Build demo; manual `./build/demo --anim` (GUI acceptance — spinner animates, controls work).

### Task 4: Docs + checkpoint

- [ ] `docs/book/86-sprite-animation.md`; README row + `--anim` line.
- [ ] Full `ctest` green; `--no-ff` merge; memory checkpoint; push (authorized).
