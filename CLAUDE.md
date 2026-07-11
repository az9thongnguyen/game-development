# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A **hand-written C++20 game engine** built from scratch to learn deeply, plus a
collection of games/tools on top of it and a hand-written Game Backend-as-a-Service.
`requirements.md` (Vietnamese) is the source-of-truth vision; `docs/book/` is a
72-chapter guidebook where each chapter maps to the code that implements it — the
guidebook chapter is the best explanation of any given subsystem.

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

Everything is one `demo` executable; the first arg picks the scene:

```sh
./build/demo            # M0 engine demo (retro 480x270)
./build/demo --gui [hvh|hvai] [easy|medium|hard]   # chess GUI
./build/demo --tui      # chess terminal
./build/demo --fps      # M2 raycaster
./build/demo --3d       # M3 software-rasterized 3D core
./build/demo --viz3d    # M3.5 interactive 3D sandbox
./build/demo --iso      # M4 isometric farm sim (F5/F9 save/load)
./build/demo --editor   # immediate-mode GUI + physics sandbox
./build/demo --colony   # engine-core integration game
```

Tests (dependency-free, no SDL/window needed):

```sh
ctest --test-dir build --output-on-failure     # all
ctest --test-dir build -R chess                # one suite by name (math, ecs, iso, fps, …)
./build/test_chess                             # or run the binary directly
```

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
                assets, image, text, ui, ecs/, jobs/, memory/, physics/
src/games/      chess, fps, viz3d, iso, editor, colony
docs/book/      the guidebook (read the chapter for the subsystem you touch)
server/         hand-written HTTP server (POSIX sockets) — separate process, no engine code
baas/           Drogon Game-BaaS backend — separate process, links no engine code
sdk/cpp/        gbaas C++ SDK the game uses to talk to baas (native libcurl / web fetch)
```

`CMakeLists.txt` (root) lists sources **explicitly** — no globbing, on purpose.
Understand these deliberate patterns before editing the build:

- **Core logic is split into SDL-free static libs** (`chess_core`, `fps_core`,
  `render3d_core`, `iso_core`, `ecs_core`, `jobs_core`, `mem_core`, `physics_core`,
  `ui_core`, `text_core`, `viz3d_core`, `colony_core`). Each has a matching
  `test_*` target so simulation/logic is unit-tested with no window.
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

## Git workflow

`main` holds stable, reviewed checkpoints. Work happens on **one feature branch per
milestone**; each build step is its own commit; branches merge to `main` with
`--no-ff` so merge commits mark milestone boundaries. Never commit or push unless
asked.
