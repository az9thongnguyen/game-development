# Tween & Easing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or executing-plans.

**Goal:** A pure, deterministic easing + scalar `Tween` core in `engine/anim`, unit-tested headless, demoed by a sweeping `--fx` fountain.

**Architecture:** New static lib `tween_core` (`src/engine/anim/tween.{hpp,cpp}`), mirroring `particles_core`: PUBLIC `src` includes, PRIVATE `engine_flags`, a `test_tween` CTest that compiles `tween.cpp` directly (dependency-free). Demo links `tween_core`; `fx_scene` uses it.

**Tech Stack:** C++20, `<cmath>`, no SDL, no new deps.

---

### Task 1: Easing catalog + Tween core (TDD)

**Files:**
- Create: `src/engine/anim/tween.hpp`, `src/engine/anim/tween.cpp`
- Test: `tests/test_tween.cpp`
- Modify: `CMakeLists.txt` (add `tween_core` + `test_tween`, link into `demo`)

- [ ] Write `tests/test_tween.cpp`: easing endpoints for all `Ease`, clamp, `SmoothStep(0.5)`, `lerp`, `Tween` start/mid/end + `done()`, ping-pong return + no-drift + never-done, determinism.
- [ ] Write `tween.hpp` (enum `Ease`, `float ease(Ease,float)`, `float lerp`, `struct Tween`).
- [ ] Write `tween.cpp` (Penner curves, clamp on entry; `Tween::value/update/done/reset`).
- [ ] Add CMake `tween_core` + `test_tween` blocks; add `tween_core` to `demo` link line.
- [ ] `cmake --build build` then `ctest -R tween` → PASS.

### Task 2: Visible use in --fx (sweep)

**Files:**
- Modify: `src/games/fx/fx_scene.hpp` (include tween.hpp, add `anim::Tween sweep_`, `bool sweep_on_`)
- Modify: `src/games/fx/fx_scene.cpp` (ctor sets sweep tween; update drives emitter X; UI checkbox)

- [ ] Ctor: `sweep_ = {0,1,2.4f,anim::Ease::SineInOut,true}`.
- [ ] `update`: `sweep_.update(dt)`; `ex = sweep_on_ ? 80 + sweep_.value()*(w_-160) : w_*0.5f`.
- [ ] Render UI: `ui_.checkbox("sweep", sweep_on_)`; bump panel height to fit.
- [ ] Build; manual `./build/demo --fx` (GUI acceptance — fountain sweeps when "sweep" on).

### Task 3: Docs + checkpoint

- [ ] `docs/book/83-tween-easing.md` chapter.
- [ ] README scene table note (if present).
- [ ] Full `ctest` green; `--no-ff` merge; memory checkpoint.
