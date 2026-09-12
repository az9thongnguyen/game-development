# AGENTS.md — read this first, then stop reading

This repository is large (151 guidebook chapters, 95 test suites, four subsystems that
are separate processes). Reading it front to back is the wrong move. This file is the
**read order** and the **rules that are not negotiable**. Everything else is a pointer.

Claude Code users: `CLAUDE.md` is the operating manual and is loaded automatically —
this file is the same map for any other agent.

---

## 1. Read in this order (about ten minutes)

| # | File | What it answers |
|---|---|---|
| 1 | **`docs/new-plan/PROGRESS.md`** — the `⏸ QUAY LẠI TỪ ĐÂY` block at the very top | Where the work stopped, what is next **and why**, the first command to run, the known risks nobody has fixed, and this machine's quirks |
| 2 | **`CLAUDE.md`** | How to build, run and test · every CLI verb · the architectural rules and what each one is protecting |
| 3 | **`docs/PROJECT-BRIEF.md`** | Current state · full feature inventory · **§8, the verified-vs-unproven ledger** · roadmap position · how to choose what to build next |
| 4 | **`docs/adr/README.md`** | 63 architectural decisions, one line each, pointing at the chapter that argues it — including 13 marked `Superseded by N`, so a reversed decision says so instead of surviving as folklore |
| 5 | The chapter for the subsystem you are about to touch | `docs/book/NN-*.md`. **The chapter is the best explanation of any subsystem**, better than the code comments, and it ends with *"What is verified, and what is not"* |

Do **not** start from `requirements.md` (the original learning vision, Vietnamese) or the
milestone table in `README.md` (a historical log that stops at Horizon 1). Both are true
and both are old.

## 2. Rules that break the design if you break them

These are argued in `CLAUDE.md`; this is the checklist:

- **SDL2 is the only runtime dependency**, and only as a thin shim behind
  `src/platform/platform.hpp`. Never an SDL drawing primitive. Every pixel is drawn by
  this project's own code into a CPU framebuffer. Engine and game code never
  `#include <SDL.h>`.
- **No blocking `while(true)` loop above the platform layer.** A frame is one
  `App::frame(dt)`. This is what lets the web build swap in `emscripten_set_main_loop`
  with no change to engine or game code.
- **All file I/O goes through `assets::`.** The web build uses a virtual filesystem.
- **The operation lives in a pure `*_core` library; the trigger only calls it.** A CLI
  flag, a keypress and a button must reach the *same* function. Chapter 145 is what
  happens when a benchmark lives inside `main.cpp` for thirty-six chapters.
- **One layout function that the renderer and the hit test both read.** A control drawn
  in one place and hit in another is invisible in a screenshot, and this project has
  shipped that bug four times.
- **`.hrt` has exactly four offline doors** (`asset.import`, `asset.texture`,
  `asset.pixels`, `asset.mix`). A fifth is a decision with its own chapter. Provenance is
  DERIVED from the marks those doors leave, not remembered.
- **CMake sources are listed by hand**, never globbed.

## 3. How work is done here

One slice = one branch = several commits = one `docs/book/NN-*.md` chapter = one `--no-ff`
merge = push. `main` holds reviewed checkpoints.

**Five gates before a merge:** `ctest` green · the chapter written · the Emscripten build
green · `PROGRESS.md` and `PROJECT-BRIEF.md` updated · the headless golden path re-run
(`--project-inspect` → `--project-publish` → `--project-verify` → `--release-promote` →
`--hub`, with **zero** `.tmp` files left behind).

**And for any slice with new logic, three more that are not optional:**

1. **~15+ single-token mutations**, recording the survivors. Every survivor is triaged:
   most are missing tests, but a cluster in one function usually means two guards cover
   each other — then the fix is **deleting a line**, not adding a test.
2. **Look at a rendered frame.** Five consecutive chapters had a bug that nothing else
   could see. There is no screen capture on this machine: render offscreen to a
   framebuffer, dump a PPM, convert to PNG.
3. **Test both directions of every new guard.** The bug is never the collision a guard
   prevents; it is the guard never lifting.

**Run `ctest` twice.** A suite only ever run on a clean tree proves nothing about the
state it forgot to clean up, and that has caught real failures here.

## 4. Things that will waste your time if nobody tells you

- **Port 8080 is occupied on this machine.** A test that touches the backend must inject a
  transport. To see what CI sees, configure with `-DCMAKE_DISABLE_FIND_PACKAGE_Drogon=ON`
  (95 tests become 60).
- **`--bench-ui` numbers from a Debug build are about 5× the real cost.** Use a Release
  build directory; the flag prints which one it is, because quoting the wrong one is how
  a budget gets recorded wrong (it did, for nine chapters).
- **A mutation harness owns the working tree while it runs.** Do not edit sources, do not
  touch the git index, and restore by copying a `.mutbak` — never `git checkout`.
- Backend tests each carry `TIMEOUT 120`: a transaction holds the only SQLite connection,
  so anything reaching for `db::client()` while one is alive self-deadlocks, and the
  default ctest timeout let that run silently for 25 minutes.

## 5. Honesty rules for what you write

- A chapter ends with **"What is verified, and what is not"**, and the second half is the
  half that matters. ✅ means *it ran and the result was seen*; ⚠️ means *written, not run,
  and here is where*.
- The verification ledger in `PROJECT-BRIEF.md §8` says ❌ about the project's own past
  when something was broken for a long time without anyone noticing. Keep it that way.
- A list of remaining work that names finished work is worse than no list. Re-read
  `PROGRESS.md`'s tail before adding to it.
