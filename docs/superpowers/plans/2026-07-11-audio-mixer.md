# Audio Mixer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or executing-plans.

**Goal:** A pure software voice `Mixer` + `tone` synth (`audio_core`), streamed through the existing `platform::play_sound` seam by a `--audio` demo — overlapping sounds finally sum.

**Architecture:** Pure `audio_core` (`src/engine/audio/mixer.{hpp,cpp}`) mirrors the other cores. No platform edits: the scene mixes one `rate/60` chunk per fixed step and pushes it via `play_sound`. Demo reuses `ui_core` + draws a live waveform.

**Tech Stack:** C++20, `<cmath>`/`<algorithm>`, no SDL in the core, no new deps.

---

### Task 1: Pure mixer + tone (TDD)

**Files:** Create `src/engine/audio/mixer.{hpp,cpp}`, `tests/test_audio.cpp`; Modify `CMakeLists.txt`.

- [ ] Write `tests/test_audio.cpp`: DC voices sum + drop; volume scales; clip at ±int16; short-voice-then-silence; null/zero guard; `stop_all`; `tone` length/amplitude/non-zero/rate-guard.
- [ ] Write `mixer.hpp` (`Voice`, `Mixer::{play,mix,active,stop_all}`, `tone`).
- [ ] Write `mixer.cpp` (int32 accumulate → clip; drop finished; `tone` = sine·decay). **Watch for non-ASCII identifiers.**
- [ ] CMake `audio_core` + `test_audio`; build; `ctest -R '^audio$'` PASS; ASan/UBSan PASS.

### Task 2: `--audio` scene + wiring

**Files:** Create `src/games/audio/audio_scene.{hpp,cpp}`; Modify `src/main.cpp`, `CMakeLists.txt`.

- [ ] Scene: `init_audio()` in ctor; 4 `tone` bank entries; `update` mixes one `rate/60` chunk + `play_sound`; render draws waveform of last chunk; buttons (4 tones + chord) add voices; master slider; "(no device)" fallback.
- [ ] main.cpp `--audio` dispatch + include; CMake add scene source + link `audio_core`.
- [ ] Build demo; manual `./build/demo --audio` (GUI/audio acceptance — chord sums voices; waveform moves).

### Task 3: Docs + checkpoint

- [ ] `docs/book/85-audio-mixer.md`; README row + `--audio` line.
- [ ] Full `ctest` green; `--no-ff` merge; memory checkpoint; **then push (authorized).**
