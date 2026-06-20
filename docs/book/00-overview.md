# Chapter 00 — Overview & How to Read This Book

> **What this is.** A from-scratch game engine in modern C++, built step by step,
> with this guidebook explaining *why* at every turn. The goal is to **learn
> deeply by building** — so we hand-write every core subsystem and lean on SDL2
> only as a thin shim. This chapter is the map: the philosophy, the architecture,
> the rules that shape everything, and the order to read in.

---

## 1. Philosophy

- **Hand-write the core to understand it.** Math, the software renderer, the 3D
  rasterizer (M3), game logic, and AI are all written by us. When "convenient" and
  "educational" conflict, we pick educational — as long as it still ships.
- **SDL2 is a thin shim only.** It may open a window, hand us a pixel buffer and
  push it to the screen, give us raw input/audio, and tell the time. Nothing more.
  Every pixel drawn, we draw.
- **Web-ready from day one.** The architecture is shaped so the WebAssembly port
  (M5) is a *drop-in*, not a rewrite. That single goal explains most of the
  structural decisions below.

## 2. Architecture (one-directional dependencies)

```
   [ games / tools: demo · chess · fps · 3d-viz ]   ← call engine API only
                       │
   [ engine core ]  app(loop) · scene · math · color · renderer2d · font
                    · input(via platform) · assets · image
                    · 3D core: pipeline · renderer3d · geometry · camera (M3)
                       │
   [ platform ]  platform.hpp  (fixed interface)
                       │
            backend_sdl.cpp  (desktop + web: Emscripten ships SDL2,
                              so the web reuses this file — only run()'s
                              loop is #ifdef'd to emscripten_set_main_loop)
```

Dependencies only point **down**. The rules that keep the web port a drop-in:

1. **Only a platform backend includes SDL.** No SDL type appears above
   `src/platform/`. (Check: `grep -rn "SDL_" src/engine src/demo` → nothing.)
2. **No blocking loop above the platform.** The loop lives in `platform::run`;
   engine/game code only provides a `tick(dt)` callback. (Browsers can't block.)
3. **All file I/O goes through `assets::`** — one place to adapt to the web's
   virtual filesystem.
4. **Single-threaded** for now (web threading needs special flags).

## 3. The repository

```
src/platform/   platform.hpp (interface) + backend_sdl.cpp (impl) + input.hpp
src/engine/     app, scene, math, color, renderer2d, font8x8, assets
src/demo/       the M0 acceptance demo scene
src/main.cpp    entry point: init platform → run the App
tests/          dependency-free unit tests (CTest)
assets/         data files (loaded via the asset seam)
docs/book/      THIS guidebook (read in chapter order)
cmake/          Emscripten toolchain (used at M5)
```

## 4. Build & run

```sh
brew install cmake sdl2                       # one-time
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/demo                                  # run from the project root
ctest --test-dir build --output-on-failure    # unit tests
```

Dev build with sanitizers (catches memory/UB bugs): `cmake -B build-asan
-DENGINE_SANITIZE=ON && cmake --build build-asan`. Head-less:
`HAND_ENGINE_FRAMES=120 ./build/demo` runs N frames then exits (for CI / leak
checks).

## 5. Reading order

| Chapter | Topic |
|--------:|-------|
| 01 | Build & toolchain (CMake, SDL2, the compile→link→run pipeline) |
| 02 | Platform layer: window, framebuffer, present, the **seam** |
| 03 | Game loop & **fixed timestep** (and why the loop lives in platform) |
| 04 | Math library (vectors, matrices, projection — tested) |
| 05 | 2D software renderer (pixels, lines, rects, sprites, text) |
| 06 | Normalized input (keyboard/mouse; down vs pressed vs released) |
| 07 | Asset & audio **seams** (web-VFS-ready I/O; audio stub) |
| 08 | The M0 demo + FPS counter + **acceptance** |
| **M1 — chess** | |
| 09–15 | Board & FEN · move generation & perft · game controller · AI search · TUI · GUI + image pipeline (.hrt) · M1 acceptance |
| **M2 — FPS raycaster** | |
| 16–17 | Raycasting (DDA, textured walls) · sprites, depth & real audio |
| **M3 — real 3D core** | |
| 18 | The 3D **transform pipeline** (model→view→projection→NDC→screen) |
| 19 | Triangle **rasterization & the z-buffer** (edge fns, barycentric, depth) |
| 20 | **Geometry**: meshes & primitive generators (cube/plane/sphere/grid/axes) |
| 21 | **Cameras**: orbit & free/fly |
| 22 | **Shading, culling & clipping** (Lambert, flat/Gouraud, backface, near-clip) |
| 23 | **M3 acceptance**: the 3D core scene in action |
| **M3.5 — interactive 3D sandbox** | |
| 24 | **Mouse picking & rays** (screen→ray, ray/sphere, ray/plane) |
| 25 | The **viz3d sandbox** — spawn/select/transform objects (M3.5 acceptance) |
| **M4 — isometric simulation** | |
| 26 | **Isometric projection** (the 2:1 diamond grid, grid↔screen, picking) |
| 27 | **Tile map & depth sorting** (dense floor, painter's algorithm, the iso key) |
| 28 | **A small ECS** (entities/components/systems, sparse sets, swap-and-pop) |
| 29 | **A\* pathfinding** (g/h/f, octile heuristic, no corner cutting, smooth follow) |
| 30 | **Save / load & serialization** (versioned text, transactional load, write seam) |
| 31 | **M4 acceptance**: the isometric farm sim in action |
| **M5 — WebAssembly port** | |
| 32 | **The web port** (emscripten_set_main_loop, canvas, preload, why tick paid off) |

Each chapter follows the same shape: **concept → code walkthrough → run &
observe → pitfalls → exercises.**

## 6. Milestone roadmap

| MS | Deliverable |
|----|-------------|
| **M0 ✅** | Engine foundation |
| **M1 ✅** | Chess — Human↔Human & Human↔AI (minimax/alpha-beta), GUI + TUI |
| **M2 ✅** | FPS raycaster (Wolfenstein-style) — adds real audio |
| **M3 ✅** | Real 3D core: software rasterizer, z-buffer, perspective, cameras |
| **M3.5 ✅** | Interactive 3D sandbox: spawn/select/transform objects, mouse picking |
| **M4 ✅** | Isometric farm sim: tile map, depth sort, ECS, A* pathfinding, save/load |
| **M5 ✅** | WebAssembly port — chess + 3D core run in-browser, no engine/game rewrite |

See `requirements.md` for the full specification, and `README.md` for the git
workflow (a feature branch per milestone, merged to `main` at each review).
