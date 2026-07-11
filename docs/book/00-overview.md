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
   `src/platform/`. (Check: `grep -rEn 'SDL_[A-Za-z]' src/engine src/demo` → nothing;
   the broader `"SDL"` also matches a couple of comments, so match real usage.)
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
| **Extension — native webserver (§11)** | |
| 33 | **A native webserver from scratch** (HTTP over sockets, MIME, path-traversal, a leaderboard API — a separate process, no engine code) |
| **Engine-core program — A: memory allocators** | |
| 34 | **Why custom allocators** + the common foundation (alignment, ownership, stats) |
| 35 | The **linear family**: Arena & Stack (bump, mark/rewind, LIFO) |
| 36 | The **free-list family**: Pool & FreeList (intrusive list, split + coalescing) |
| 37 | The **FrameAllocator** (double-buffering) + how subsystems B–F adopt all five |
| **Engine-core program — B: ECS** | |
| 38 | **Entities & safe handles** (index + generation, recycling, ABA-safe retirement) |
| 39 | **Sparse-set storage & type erasure** (`SparseSet<T>`, `IPool`, `type_id<T>`) |
| 40 | **Views, systems & acceptance** (component queries, fold expressions, systems) |
| **Engine-core program — C: job system** | |
| 41 | **Threads, the pool & the queue** (mutex/condvar work queue, data races, shutdown) |
| 42 | **Counters, wait-and-help & parallel_for** (the API + the synchronous web fallback) |
| **Engine-core program — D: asset pipeline** | |
| 43 | **The asset cache** (per-type loaders, (type,path) keys, type erasure, the mtime seam) |
| 44 | **Hot reload** (in-place content swap, mtime polling, reentrancy, the web no-op) |
| **Engine-core program — E: 2D physics** | |
| 45 | **Bodies & integration** (rigid bodies, inverse mass, semi-implicit Euler) |
| 46 | **Collision detection** (circle/box manifolds, normals, penetration, edge cases) |
| 47 | **Impulse resolution & the world step** (restitution, positional correction) |
| **Engine-core program — F: editor / GUI** | |
| 48 | **Immediate-mode GUI: the idea & the core** (retained vs immediate, hot/active, ids) |
| 49 | **Widgets, the editor scene & the whole project** (the `--editor` capstone + recap) |
| **Integration** | |
| 50 | **Engine-core integration: the colony sim** (a game on A+B+C+D+F: ECS, jobs, frame alloc, asset cache, GUI) |
| 51 | **Game BaaS: overview & architecture** (slices, modular monolith, the transport seam) |
| 52 | **Drogon & the gateway** (filter chain, api-key→project, error envelope, OBJECT-lib registration) |
| 53 | **Authentication** (argon2id, HS256 JWT over libsodium, guest accounts, threat model) |
| 54 | **Persistence & the data model** (DbClient, schema, migration/seed, the tenancy rule) |
| 55 | **The leaderboard service** (ranking, best-keep, anti-spoof, tenant isolation) |
| 56 | **The unified C++ SDK** (non-blocking client, native/web transport seam) |
| 57 | **Colony online** (integrating the SDK; native + web; same-origin serving) |
| 58 | **Game BaaS Slice #1 acceptance** (end-to-end, security checklist, tests) |
| 59 | **Cloud Save** (versioned per-user/per-slot saves, optimistic concurrency, colony save/load) |
| 60 | **Inventory** (per-user item quantities, grant/consume, server-enforced non-negative spend) |
| 61 | **Remote Config, Analytics & Live Events** (the read/ingest L1 services; L1-vs-L3 split) |
| 62 | **The Dashboard & Admin API** (operator half: two-level admin auth, project provisioning, a hand-written SPA) |
| 63 | **Realtime server** (Lobby + Matchmaking over WebSocket: the hub, auth-on-upgrade, tenant isolation, disconnect cleanup) |
| 64 | **Realtime SDK & demo** (the `IWsTransport` seam — native `ws://` via libcurl, web via the browser WebSocket — and the live dashboard console) |
| 65 | **Replay System** (command-stream record/store/playback: the `replays` store, `client.replays()`, colony record→cloud→playback; determinism honestly discussed) |
| 66 | **Rate limiting** (production hardening: a pure token-bucket limiter, a pre-routing advice keyed by api-key/IP on `/v1/*`, 429s; single-node caveats stated) |
| 67 | **Observability** (metrics + structured access logs from one pre-sending advice; status/route tallies with cardinality control; admin-gated `/metrics`) |
| 68 | **Font rendering** (from the 8×8 bitmap to anti-aliased `stb_truetype` glyphs: outlines vs pixels, rasterization = AA, glyph metrics/baseline, the atlas cache) |
| 69 | **Anti-aliasing I — SSAA** (the supersample seam: physical vs logical framebuffer, downsample on present, logical mouse, free AA for the 3D raycaster, the cost/toggle) |
| 70 | **Anti-aliasing II — Wu & coverage** (Xiaolin Wu lines; analytic coverage for rounded rects/circles/rings via a tiny distance field) |
| 71 | **Design systems** (tokens as one source of truth, the one-accent rule, type/spacing/radius scales, elevation + state ramps, applying it to the IMGUI) |

Each chapter follows the same shape: **concept → code walkthrough → run &
observe → pitfalls → exercises.**

> **UI/UX overhaul (ch.68–71).** The renderer gained a real anti-aliased font
> (`stb_truetype` + a glyph atlas), universal SSAA plus analytic per-primitive AA
> (Wu lines, coverage rounded-rects/circles, soft shadows), and a design-system
> layer (`engine/ui/theme.hpp` tokens + rebuilt widgets). All games inherit it;
> **colony** and **editor** are the showcases (verified native and in-browser).
> Native 2D scenes render at `supersample=2`; the web build stays at `1×` (per-
> primitive AA + AA fonts still apply) so WASM keeps a smooth frame rate.

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
| **ext ✅** | Native webserver (§11) — hand-written, serves the WASM build + a leaderboard API as a separate process |
| **A ✅** | Memory allocators — arena/stack/pool/freelist/frame (engine-core foundation) |
| **B ✅** | Engine-core ECS — type-erased sparse-set registry, generation handles, views |
| **C ✅** | Job system — thread pool + counters + parallel_for; synchronous on web |
| **D ✅** | Asset pipeline — cache + per-type loaders + in-place hot reload |
| **E ✅** | 2D physics — rigid bodies, circle/box collision, impulse resolution |
| **F ✅** | Editor support — hand-written immediate-mode GUI + a physics sandbox |
| **integration ✅** | Colony sim (`--colony`) — a game built on A+B+C+D+F (ECS, jobs, frame alloc, asset cache, GUI) |
| **BaaS S1 ✅** | Game Backend-as-a-Service, Slice #1 — auth (argon2id + JWT) + leaderboard on Drogon, multi-tenant projects, a unified non-blocking C++ SDK; colony online native + web |
| **BaaS S2 ✅** | Cloud Save — versioned per-user/per-slot saves with optimistic concurrency (If-Match); colony state round-trips to the cloud, native + web |
| **BaaS S3 ✅** | Inventory — per-user item quantities (grant/consume, server-enforced non-negative spend); colony wood economy, native + web |
| **BaaS S4 ✅** | Remote Config + Analytics + Live Events — the read/ingest L1 services (admin write/query deferred to the L3 dashboard); colony reads motd + live events, sends analytics |
| **BaaS S5 ✅** | Dashboard (L3) — admin API (two-level auth: platform secret + per-project secret key) for project provisioning, config/events write, analytics/users; a hand-written web dashboard served by the baas |
| **BaaS S6 ✅** | Realtime (L2) — Lobby + Matchmaking over WebSocket: a mutex-guarded in-memory hub, auth-on-upgrade, tenant-scoped rooms/queue; SDK realtime channel (native `ws://` + web browser WebSocket) and a live dashboard console. **Last hand-buildable tier — L4 needs real cloud/3rd-party infra.** |
| **BaaS S7 ✅** | Realtime end-to-end in the game — native `ws://` transport proven against a live server (`sdk_realtime_live`: lobby, broadcast, matchmaking, tenant isolation, auth rejection); colony gains a **presence** panel via `client.realtime()` (native + web, browser-verified). Fixes found by the live test: project-wide ws-capable libcurl (no dual-curl), async-connect op buffering, and same-origin ws URL resolution on web. |
| **BaaS S8 ✅** | Replay System — per-user, immutable, named recordings (`/v1/replays` create/list/get/delete, project+user scoped, 512 KiB cap); `client.replays()` in the SDK (rides the existing HTTP transport — zero new web work); colony records its command stream → cloud → **deterministic-ish command-stream playback**. The one hand-buildable Phase-2 item; the rest (voice, hosting, cross-platform login, AI) needs real cloud/3rd-party infra. |
| **BaaS S9 ✅** | Production hardening — a pure, thread-safe **token-bucket rate limiter** + a Drogon pre-routing advice on `/v1/*` (keyed by api-key, else IP; 429 on excess; static/`/healthz` exempt). On by default in the server (burst 120, 60/s; env-tunable). Tests: `rate_limiter` (pure, injected time) + `baas_ratelimit` (live burst→429). Single-node caveat documented (multi-node → shared bucket). |
| **BaaS S10 ✅** | Observability — a pre-sending advice counts **every** response (metrics: total, by status-class, by normalized route with cardinality control) + emits a structured access log with per-request latency; admin-gated `GET /metrics` (JSON). Tests: `metrics` (pure) + `baas_metrics` (live tallies + admin gate). Histograms/tracing/persistence noted as the next layer. |

See `requirements.md` for the full specification, and `README.md` for the git
workflow (a feature branch per milestone, merged to `main` at each review).
