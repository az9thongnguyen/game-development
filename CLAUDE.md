# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A **hand-written C++20 game engine** built from scratch to learn deeply, plus a
collection of games/tools on top of it and a hand-written Game Backend-as-a-Service.
`requirements.md` (Vietnamese) owns the original *learning* vision; `docs/strategy/`
owns the *product* direction (how this grows into a self-hostable game-creation
platform) — read it before touching the platform spine. `docs/book/` is a 100+-chapter
guidebook where each chapter maps to the code that implements it; **the guidebook
chapter is the best explanation of any given subsystem** (e.g. `93` = release store,
`95`–`97` = Hub/Studio shell). `docs/guides/author-to-url.md` walks the operator
golden path end to end. **`docs/PROJECT-BRIEF.md` is the single orientation document**:
current state, full feature inventory, a verified-vs-unproven ledger, the roadmap
position, and the decision rules for choosing what to build next — read it before
picking work.

## Non-negotiable architectural constraints

These are the rules the whole design exists to protect. Breaking them is a design
regression, not a shortcut:

- **SDL2 is the ONLY runtime dependency, and only as a thin shim** — window, raw
  framebuffer present, raw input, audio, timing. **Never** use SDL drawing
  primitives (`SDL_Renderer`, `SDL_image`, …). Every pixel is drawn by our own code
  into a CPU framebuffer.
- **Engine/game code never `#include <SDL.h>`.** It talks only to
  `src/platform/platform.hpp` — the fixed platform seam. SDL lives behind it in
  `backend_sdl.cpp`. If an SDL type wants to appear in a header above the platform
  layer, the abstraction is leaking.
- **No blocking `while(true)` game loop above the platform layer.** A frame is one
  `App::frame(dt)` call driven by `platform::run()`. This is what lets the web build
  swap in `emscripten_set_main_loop` with zero changes to engine/game code.
- **All file I/O goes through `assets::` (`src/engine/assets.*`)**, never scattered
  `fopen`. The web build uses a virtual filesystem.
- **Web-portability is baked in from the start.** The same engine/game code compiles
  native and WASM; only the platform `run()` loop is `#ifdef`'d.

## Build, run, test

```sh
brew install cmake sdl2                        # prerequisites (macOS)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Everything is one `demo` executable; the first arg picks the mode. Windowed scenes:

```sh
./build/demo            # M0 engine demo (retro 480x270)
./build/demo --gui [hvh|hvai] [easy|medium|hard]   # chess GUI      (--tui = terminal)
./build/demo --fps      # M2 raycaster (loads the Map-Lab-authored maps/level_00.map)
./build/demo --3d       # M3 software-rasterized 3D core     --viz3d = interactive sandbox
./build/demo --iso      # M4 isometric farm sim (F5/F9 save/load)
./build/demo --editor   # immediate-mode GUI + physics sandbox
./build/demo --colony   # engine-core integration game (also the BaaS/SDK client)
./build/demo --studio   # Mini Studio: procedural Texture Lab (.hrt export, sheet export)
./build/demo --sandbox  # declarative 2D sandbox: actors + data-only behaviors
./build/demo --maplab   # tile-grid level editor -> maps/level_NN.map
./build/demo --fx | --light | --audio | --anim     # particles / 2D lights / mixer / flipbook
./build/demo --hub-ui [proj] | --shell [proj]      # interactive Hub / Studio (1280x720, resizable)
                                                  # --shell opens the Map workspace: paint/rect/fill a
                                                  # map2, Cmd+Z undo, Cmd+S save, Cmd+K command palette,
                                                  # Cmd+1..5 sections. Autosaves; offers recovery on open.
```

**Headless platform-spine verbs** (no window; these are what CI smoke-tests, so keep
them working). Paths are relative to the asset root — see `assets::` below:

```sh
./build/demo --project-new projects/mine.gameproject fps "My Game"   # create
./build/demo --project projects/creator.gameproject                  # launch from manifest
./build/demo --project projects/farm.gameproject                     # ...the farm game (entry `farm`)
./build/demo --project-inspect  <proj>        # validate/doctor + resource closure
./build/demo --project-package  <proj>        # deterministic package manifest (release-id seed)
./build/demo --project-publish  <proj> development "reason"   # atomic publish + audit
                                              # (the reason is REQUIRED: a blank audit line is not evidence)
./build/demo --release-promote  development preview "reason"  # dev -> preview -> production
./build/demo --release-rollback production <release-id> "reason"
./build/demo --release-status | --release-log [channel]
./build/demo --project-verify   <proj> development   # preview parity: exit 0 match / 2 drift / 1 err
./build/demo --hub <proj>                     # aggregate status + next recommended action
./build/demo --runner <baas_url> <api_key>    # headless BaaS test-run worker
./build/demo --bench-ui [frames] [proj]       # Studio frame cost, ss=1 vs ss=2 (no window)
./build/demo --cmd [id] [args...]              # run any registered command; no id lists them
```

Tests (dependency-free, no SDL/window needed):

```sh
ctest --test-dir build --output-on-failure     # all
ctest --test-dir build -R chess                # one suite by name (math, ecs, iso, fps, …)
./build/test_chess                             # or run the binary directly
```

BaaS backend (separate process, **guarded on Drogon** — the engine build never
depends on it; when Drogon is absent its targets, including the `baas_*`/`sdk_live`
tests, silently vanish from `ctest`):

```sh
brew install drogon libsodium                  # enables the 'baas' target
cp baas/config.example.json baas/config.json   # gitignored local dev config
./build/baas/baas                              # or: docker compose -f baas/ops/docker-compose.yml up
```

CI (`.github/workflows/ci.yml`, ubuntu + macos) does a clean configure/build, runs
`ctest`, then smoke-tests the whole golden path (create → inspect → publish →
verify → promote → status → log → hub) and asserts no `.tmp` files leak from the
atomic publish. Breaking a headless verb breaks CI even if `ctest` is green.

Sanitizer build (ASan + UBSan) for memory/UB bugs during dev:

```sh
cmake -B build-asan -DENGINE_SANITIZE=ON && cmake --build build-asan && ./build-asan/demo
```

Web (Emscripten) build:

```sh
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-web && cmake --build build-web --target demo
cd build-web && python3 -m http.server 8765   # open http://localhost:8765/demo.html
```

Pick the web scene by editing `Module.arguments` in `web/shell.html`.

## Layout & the library split

```
src/platform/   the platform seam (platform.hpp) + backend_sdl.cpp
src/engine/     hand-written core: math, renderer2d, renderer3d, geometry, camera,
                assets, image, text, ui, ecs/, jobs/, memory/, physics/, anim/,
                fx/, audio/ + the platform spine: project/, resource/, release/, hub/
src/games/      one dir per scene/tool (chess, fps, iso, colony, studio, sandbox,
                maplab, hub, studio_shell, fx, light, audio, anim, runner, …)
docs/book/      the guidebook (read the chapter for the subsystem you touch)
server/         hand-written HTTP server (POSIX sockets) — separate process, no engine code
baas/           Drogon Game-BaaS backend — separate process, links no engine code
sdk/cpp/        gbaas C++ SDK the game uses to talk to baas (native libcurl / web fetch)
```

`CMakeLists.txt` (root) lists sources **explicitly** — no globbing, on purpose.
Understand these deliberate patterns before editing the build:

- **Core logic is split into SDL-free static libs** (`chess_core`, `fps_core`,
  `render3d_core`, `iso_core`, `ecs_core`, `jobs_core`, `mem_core`, `physics_core`,
  `ui_core`, `text_core`, `viz3d_core`, `colony_core`, plus the platform-spine
  cores `project_core`, `resource_core`, `release_core`, `release_ops_core`,
  the game cores `farm_core` (day loop, crops, NPC schedules, dialogue — no renderer),
  `hub_core`/`hub_build_core`, and the content cores `studio_core`, `sandbox_core`,
  `maplab_core`, `map_edit_core` (tile edits as undoable `doc::Command`s),
  `particles_core`, `tween_core`, `light_core`, `audio_core`,
  `runner_core`). Each has a matching `test_*` target so simulation/logic is
  unit-tested with no window.
- **`-DENGINE_BUILD_DESKTOP=OFF`** builds everything except the SDL2 `demo` target —
  that's how the backend container image builds without SDL2 present.
- **`renderer3d.cpp` / `ui.cpp` reference `Renderer2D` symbols but don't link it** —
  the final target that links them provides `renderer2d.cpp`. Several `test_*`
  targets therefore *compile* `renderer2d.cpp`/`assets.cpp` directly rather than
  linking a lib, to stay dependency-free. Tests needing asset files get
  `-DASSET_ROOT=...`.
- **`baas` is guarded on Drogon being installed** (`brew install drogon libsodium`).
  The ordinary engine build never hard-depends on it. The engine core gains **no**
  dependency from baas/sdk — only the SDK links libcurl.

## Runtime architecture

`App` (`src/engine/app.hpp`) owns the active `Scene` and a **fixed-timestep clock**:
`platform::run` feeds it a variable `dt`; `App::frame` accumulates it into fixed
`1/60 s` `update()` steps (deterministic logic) plus exactly one `render()` per frame.
A `Scene` (`src/engine/scene.hpp`) implements `update(dt, input)` and `render(ctx)`,
where `Context` carries the `Renderer2D`, input snapshot, timing, and shared UI font.
Each game is a `Scene`; `src/main.cpp` maps a CLI flag to a `platform::Config` + scene.

## The platform spine (create → publish → promote → verify)

Beyond the games, the repo is growing a game-creation *platform*, and its data flow
is the thing most likely to be broken by a careless edit:

1. **`game.project` manifest** (`project_core`) — a versioned text file that declares
   identity, an `entry` (which game to launch), and its content as `asset <type> <path>`
   lines. `--project` launches from it; `src/main.cpp`'s `launch_entry` seam maps an
   entry name to a scene, so a new game needs no new CLI flag. **The farm game is the
   proof**: `projects/farm.gameproject` added a game with a manifest and a scene, and
   inspect/package/publish/hub all worked on it unchanged.
2. **Resource closure** (`resource_core`) — hand-written FNV-1a `content_hash` over each
   declared asset. `--project` **hard-refuses** to launch with a missing dependency.
3. **Package** (`resource_core::build_package`) — resources sorted by path + a combined
   `packagehash`: order-independent, content-sensitive. This hash *is* the release id.
4. **Release store** (`release_core`, `release_ops_core`) — `releases/<hash>/` is
   immutable and content-addressed; the channels `development → preview → production`
   are pointers moved by promote/rollback. Publishes are **atomic** (stage `.tmp` →
   `assets::rename`) and **audited** (append-only `releases/audit.log` recording
   timestamp, predecessor, reason). Re-publishing identical bytes is a verified no-op;
   publishing *different* bytes under an existing id is refused.
5. **Hub** (`hub_core`) — one pure `hub_lines`/`recommend` shared by the headless
   `--hub` and the windowed `--hub-ui`/`--shell`, so CLI and window can't drift.
6. **Command registry** (`commands_core`) — every operation registers once under a
   stable id; `--cmd <id>`, the Studio's `Cmd+K` palette and a button all go through
   `cmd::run`, so an operation cannot exist in only one of them. The old CLI flags are
   aliases onto it. Mutating commands **refuse blank arguments**: an audit line with
   no reason looks like evidence and answers nothing.

Two rules follow from that shape: **the operation lives in a pure `*_core` lib and the
trigger (CLI flag or keypress) only calls it** — never reimplement an op in a Scene; and
every write goes through `assets::` so the same code works on native and the web MEMFS.
The asset root (`ASSET_ROOT`/`assets/`) is the origin of every runtime path, which is
why manifests read as `projects/foo.gameproject`, not a filesystem path.

## Git workflow

`main` holds stable, reviewed checkpoints. Work happens on **one feature branch per
milestone**; each build step is its own commit; branches merge to `main` with
`--no-ff` so merge commits mark milestone boundaries. Never commit or push unless
asked.
