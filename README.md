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
| **M0** | Engine foundation: window + software renderer + tick loop + math + input | 🚧 in progress |
| M1 | Chess (desktop) — **2 chế độ chơi**: Người↔Người & Người↔Máy (AI minimax/alpha-beta); **GUI + TUI** | ⬜ planned |
| M2 | FPS raycaster (Wolfenstein-style) | ⬜ planned |
| M3 | Real 3D core: software rasterizer, z-buffer, perspective, cameras | ⬜ planned |
| M3.5 | 3D visualization sandbox | ⬜ planned |
| M4 | Isometric sim (depth-sort + A* + save/load) | ⬜ planned |
| M5 | Web port via Emscripten (no engine rewrite) | ⬜ planned |
| _future (optional)_ | Native webserver (e.g. **Drogon**) to serve the web build / online features — **separate process, not part of engine core** | 💡 idea |

## Prerequisites (macOS)

```sh
brew install cmake sdl2
```

(Apple clang or Homebrew clang for a C++20 compiler — already present on most Macs.)

## Build & run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/demo
```

For a sanitizer build (catches memory + undefined-behavior bugs during dev):

```sh
cmake -B build-asan -DENGINE_SANITIZE=ON
cmake --build build-asan
./build-asan/demo
```

## Project layout

```
src/platform/   thin platform layer behind a fixed interface (platform.hpp)
src/engine/     hand-written engine core (math, renderer, input, ...)
src/demo/       the M0 acceptance demo scene
docs/book/      the guidebook — read these chapters alongside the code
cmake/          toolchain files (Emscripten added at M5)
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
