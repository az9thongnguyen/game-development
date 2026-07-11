# Sandbox Animated Actors Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or executing-plans.

**Goal:** A sandbox actor can play a sprite-sheet animation (frames/fps on the shared Archetype), authored in the inspector and round-tripped through save/load.

**Architecture:** Data-first — add `frames`/`fps` to the shared `Sprite`/`Archetype`; the codec emits them only when animated. Draw slices the vertical sheet by a scene-wide `anim_time_` clock (cosmetic, unsaved). Inspector auto-sets frames from a `sheet_frames_` registry.

**Tech Stack:** C++20, existing sandbox + `blit_scaled`; no new libs.

---

### Task 1: Model + codec (TDD)

**Files:** Modify `world.hpp`, `world.cpp`, `serialize.cpp`; `tests/test_sandbox.cpp`.

- [ ] Add `test_animated_sprite_roundtrip`: spawn frames=8/fps=12 → Sprite carries them; `to_scene` has `frames=8`+`fps=12`; `to_scene(from_scene(s))==s`; static actor emits neither token.
- [ ] `Sprite` + `Archetype`: `int frames=1; float fps=8.0f;`.
- [ ] Spawn: `reg.add<Sprite>(e, {a.color,a.round,a.texture,a.frames,a.fps})`.
- [ ] `archetype_tokens`: emit `frames=`/`fps=` when `frames>1`; `parse_archetype`: read `frames`/`fps`; `to_scene`: copy from Sprite.
- [ ] Build `test_sandbox`; `ctest -R '^sandbox$'` PASS; ASan/UBSan PASS.

### Task 2: Scene draw + author

**Files:** Modify `sandbox_scene.hpp`, `sandbox_scene.cpp`.

- [ ] Header: `std::unordered_map<std::string,int> sheet_frames_; double anim_time_ = 0;`.
- [ ] `load_textures`: load `sprites/spin_8.hrt` into `tex_`/`tex_names_`, `sheet_frames_["spin_8"]=8`.
- [ ] `update`: `anim_time_ += dt;` (always).
- [ ] Draw: when `frames>1 && fps>0 && img.h>=frames`, slice `f=int(anim_time_*fps)%frames` from the vertical sheet, else full texture.
- [ ] Inspector Tex-cycle: set `s->frames` from `sheet_frames_`; show `anim fps` slider when `frames>1`.
- [ ] Build demo; manual `./build/demo --sandbox` (place actor → Tex=spin_8 → animates; Play/Stop + F5/F9 preserve it).

### Task 3: Docs + checkpoint

- [ ] `docs/book/87-sandbox-animated-actors.md`; README row + `--sandbox` note.
- [ ] Full `ctest` green; `--no-ff` merge; memory checkpoint; push (authorized).
