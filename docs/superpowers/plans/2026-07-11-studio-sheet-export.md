# Studio Sheet Export Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or executing-plans.

**Goal:** Export animated sprite sheets from the Texture Lab (`make_sheet` by seamless scroll), self-describing frame count, discovered by `--anim`/`--sandbox`.

**Architecture:** Pure `make_sheet` in `studio_core` reuses `generate()`. Shared `anim::frames_in_sheet` (aspect ratio) is the exporter↔consumer contract. Consumers derive frame count from image shape — no registry/sidecar.

**Tech Stack:** C++20, existing studio + image + anim; no new libs.

---

### Task 1: frames_in_sheet (TDD)

**Files:** Modify `src/engine/anim/flipbook.{hpp,cpp}`, `tests/test_flipbook.cpp`.

- [ ] Add cases: spin_8 (48×384→8), 64×256→4, 32×32→1, 64×48→1, 48×380→1, 0×100→1.
- [ ] `int frames_in_sheet(int w, int h)` = `(w>0 && h>w && h%w==0) ? h/w : 1`.
- [ ] Build `test_flipbook`; `ctest -R '^flipbook$'` PASS.

### Task 2: make_sheet (TDD)

**Files:** Create `src/games/studio/sheet.{hpp,cpp}`; Modify `CMakeLists.txt` (sheet.cpp → studio_core), `tests/test_studio.cpp`.

- [ ] Add `test_make_sheet`: dims `32×256` for 8, `h/w==8`, frame 0 == base, frame 4 differs + reuses base colours, `frames<=1` → base.
- [ ] `make_sheet(p, frames)`: generate base; stack N square frames scrolled `(f*size)/frames` with `%size` wrap.
- [ ] Build `test_studio`; `ctest -R '^studio$'` PASS.

### Task 3: Exporter + consumers

**Files:** Modify `studio_scene.{hpp,cpp}`, `sandbox_scene.{hpp,cpp}`, `anim_scene.cpp`.

- [ ] Studio: `kSheetFrames{4,8,16}`, `export_sheet()` → `sprites/sheet_NN.hrt`; UI cycle + Export Sheet button; panel height bump.
- [ ] Sandbox: remove `sheet_frames_` map; `load_textures` loads spin_8 + probes `sprites/sheet_00..07`; Tex-cycle sets `frames = anim::frames_in_sheet(img.w,img.h)`; include flipbook.hpp.
- [ ] `--anim`: prefer `sprites/sheet_00.hrt`, fall back to spin_8, derive `frames_`.
- [ ] Build demo; full `ctest` green.

### Task 4: Verify + docs + checkpoint

- [ ] Dev smoke: `make_sheet → encode → decode → frames_in_sheet == N` for N∈{4,8,16}.
- [ ] `docs/book/88-studio-sheet-export.md`; README row + `--studio` note.
- [ ] `--no-ff` merge; memory checkpoint.
