# Hand-written C++ Game Engine

A game engine written **from scratch** in modern C++ — every core subsystem (math,
software renderer, 3D rasterizer, game logic, AI) is hand-written. The goal is to
**learn deeply by building**, then ship real games and tools on top of it.

> **SDL2 is the only allowed dependency**, and only as a thin shim: window
> creation, raw framebuffer presentation, raw input, audio, and timing. Every
> pixel we draw, we draw ourselves into a CPU framebuffer. No SDL drawing
> primitives, no game frameworks.

See [`requirements.md`](requirements.md) for the full vision and milestones, and
the **guidebook** in [`docs/book/`](docs/book/) for the step-by-step learning
material that accompanies the code.

## Roadmap (milestones)

| MS | What | Status |
|----|------|--------|
| **M0** | Engine foundation: window + software renderer + tick loop + math + input | ✅ done |
| M1 | Chess (desktop) — **2 chế độ chơi**: Người↔Người & Người↔Máy (AI minimax/alpha-beta); **GUI + TUI** | ✅ done |
| M2 | FPS raycaster (Wolfenstein-style) — textured walls, billboard sprites, real audio | ✅ done |
| M3 | Real 3D core: software rasterizer, z-buffer, perspective, mesh/primitives, orbit + free cameras, flat/Gouraud/wireframe, backface culling | ✅ done |
| M3.5 | Interactive 3D sandbox: spawn/select/transform objects, mouse picking, orbit/pan/zoom camera | ✅ done |
| M4 | Isometric farm sim: tile map + depth-sort + small ECS + A* pathfinding + save/load | ✅ done |
| M5 | WebAssembly port (Emscripten) — chess + 3D core run in-browser, **no engine/game rewrite** | ✅ done |
| _extension (§11)_ | Native webserver — **hand-written** (no framework), serves the WASM build + a leaderboard API; **separate process, links no engine code** | ✅ done |
| **A** | Engine-core memory allocators — arena · stack · pool · freelist · frame (custom, hand-written) | ✅ done |
| **B** | Engine-core ECS — generic type-erased sparse-set registry, generation-safe handles, views | ✅ done |
| **C** | Job system — thread pool + counters + parallel_for (multicore on desktop, synchronous on web) | ✅ done |
| **D** | Asset pipeline + hot reload — cache, per-type loaders, in-place reload on file change | ✅ done |
| **E** | 2D physics — rigid bodies, circle/box collision, impulse resolution + positional correction | ✅ done |
| **F** | Editor support — hand-written immediate-mode GUI + a UI-driven physics sandbox | ✅ done |
| **integration** | Colony sim — a game standing on the engine core (ECS + jobs + frame allocator + asset cache + GUI) | ✅ done |

## Prerequisites (macOS)

```sh
brew install cmake sdl2
```

(Apple clang or Homebrew clang for a C++20 compiler — already present on most Macs.)

## Build & run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/demo            # M0 engine demo
./build/demo --gui      # chess (GUI)     — also: hvh|hvai  easy|medium|hard
./build/demo --tui      # chess (terminal)
./build/demo --fps      # M2 raycaster
./build/demo --3d       # M3 real 3D core (cube + sphere + grid; drag/WASD/ENTER/SPACE/RMB)
./build/demo --viz3d    # M3.5 interactive sandbox (1-4 spawn, click select, drag move, ...)
./build/demo --iso      # M4 isometric farm sim (1-0 brushes, LMB paint, RMB walk farmer, F5/F9 save/load)
./build/demo --editor   # F editor: immediate-mode GUI + physics sandbox (click to drop bodies)
./build/demo --colony   # integration: iso agent sim on ECS + jobs + frame alloc + asset cache + GUI
ctest --test-dir build --output-on-failure   # unit tests (math, render3d, viz3d, chess, fps, iso)
```

For a sanitizer build (catches memory + undefined-behavior bugs during dev):

```sh
cmake -B build-asan -DENGINE_SANITIZE=ON
cmake --build build-asan
./build-asan/demo
```

### Web build (WebAssembly via Emscripten — M5)

```sh
# one-time: install the SDK
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest

source ~/emsdk/emsdk_env.sh                 # per shell: puts emcc/emcmake on PATH
emcmake cmake -B build-web
cmake --build build-web --target demo       # → build-web/demo.{html,js,wasm,data}
cd build-web && python3 -m http.server 8765 # WASM must be served over http
# open http://localhost:8765/demo.html
```

The web build runs the **same** engine/game code; only the platform `run()` loop is
`#ifdef`'d to `emscripten_set_main_loop`. Pick the scene by editing
`Module.arguments` in [`web/shell.html`](web/shell.html) (e.g. `['--3d']` or `['--iso']`).

### Native webserver (requirements §11 — optional, separate process)

A hand-written C++ HTTP server (no framework, POSIX sockets) that serves the WASM
build and a tiny leaderboard API. It links **no** engine code — the game and server
meet only over HTTP.

```sh
cmake --build build --target webserver
./build/webserver --root build-web        # → http://localhost:8080/  (the game)
# API: GET/POST http://localhost:8080/api/scores  ({"name":"..","score":N})
```

## Project layout

```
src/platform/   thin platform layer behind a fixed interface (platform.hpp)
src/engine/     hand-written engine core: math, renderer2d, input, assets,
                image, and the M3 3D core (pipeline, renderer3d, geometry, camera)
src/demo/       the M0 acceptance demo scene
src/games/      chess (M1), fps raycaster (M2), viz3d 3D showcase + sandbox (M3/M3.5),
                iso farm sim (M4: tilemap, ecs, pathfind, farm, serialize, render, scene)
docs/book/      the guidebook — read these chapters alongside the code
web/            shell.html for the WebAssembly build (M5)
server/         hand-written native webserver (§11) — separate process, no engine code
cmake/          Emscripten toolchain hook (used at M5)
```

## Development workflow (git)

History is kept clean and reviewable:

- **`main`** — stable, reviewed checkpoints only.
- **One feature branch per milestone**, e.g. `feat/m0-engine-foundation`,
  `feat/m1-chess`. Each build step is its own commit on the branch.
- At a milestone's review/acceptance, the branch is merged into `main` with
  `--no-ff`, so the merge commit marks the milestone boundary.

`git log --oneline --graph main` then reads as a milestone timeline, while each
feature branch keeps the detailed step-by-step history.

## How to follow along

Read the guidebook chapters in `docs/book/` in order. Each chapter explains the
**concept**, walks through the **code**, tells you exactly what to **run and
observe**, and ends with small **exercises**. Start with
[`docs/book/00-overview.md`](docs/book/00-overview.md) (added with the first
runnable milestone) or jump straight to
[`docs/book/01-build-and-toolchain.md`](docs/book/01-build-and-toolchain.md).
