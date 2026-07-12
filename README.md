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

For the longer product direction — how this hand-written codebase grows into one
inspectable, self-hostable game-creation platform — see
[`docs/strategy/`](docs/strategy/) (market, gap analysis, target architecture,
outcome-gated roadmap, metrics, and competitive watchlist). `requirements.md` owns
the original *learning* vision; `docs/strategy/` owns the *product* direction and its
adopted execution posture. The two are complementary, not competing.

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
| **BaaS S1** | Game Backend-as-a-Service, Slice #1 — auth (argon2id + JWT) + leaderboard on Drogon, multi-tenant projects, unified non-blocking C++ SDK (native libcurl / web emscripten_fetch); colony online native + web | ✅ done |
| **BaaS S2** | Cloud Save — versioned per-user/per-slot game state with optimistic concurrency (If-Match); `client.saves()` in the SDK; colony Cloud Save/Load native + web | ✅ done |
| **BaaS S3** | Inventory — per-user item quantities, grant/consume with server-enforced non-negative spend; `client.inventory()`; colony wood economy native + web | ✅ done |
| **BaaS S4** | Remote Config + Analytics + Live Events — client-facing read/ingest L1 services (`client.config()/analytics()/events()`); admin write/query deferred to the L3 dashboard | ✅ done |
| **BaaS S5** | Dashboard (L3) — admin API with two-level auth (platform `X-Admin-Secret` + per-project `X-Secret-Key`): create/list projects, config & live-event writes, analytics summary, users; a hand-written web dashboard at `/dashboard` | ✅ done |
| **BaaS S6** | Realtime (L2) — Lobby + Matchmaking over WebSocket (`/v1/ws`): mutex-guarded in-memory hub, auth-on-upgrade, tenant-scoped rooms/queue; `client.realtime()` in the SDK (native `ws://` via libcurl, web via the browser WebSocket) + a live dashboard Realtime console. Last hand-buildable tier (L4 needs real cloud/3rd-party infra). | ✅ done |
| **BaaS S7** | Realtime end-to-end in the game — native `ws://` transport proven live (`sdk_realtime_live`: lobby, broadcast, matchmaking, tenant isolation, auth rejection); colony **presence** panel via `client.realtime()` (native + web, browser-verified). | ✅ done |
| **BaaS S8** | Replay System — per-user immutable named recordings (`/v1/replays`, project+user scoped, 512 KiB cap); `client.replays()` in the SDK (over the existing HTTP transport); colony records its command stream → cloud → command-stream playback. | ✅ done |
| **BaaS S9** | Production hardening — thread-safe token-bucket **rate limiter** + Drogon pre-routing advice on `/v1/*` (per api-key/IP, 429 on excess, `/healthz` & static exempt); on by default (burst 120, 60/s, env-tunable). Unit + integration tested. | ✅ done |
| **BaaS S10** | Observability — pre-sending advice counts every response (total, by status-class, by normalized route) + structured access log with latency; admin-gated `GET /metrics` (JSON). Unit + integration tested. | ✅ done |
| **Studio v1** | Mini Studio — procedural **Texture Lab**: hand-written seamless noise (value/Perlin/fBm), `.hrt` export + re-editable recipes, in-session collection (`--studio`) | ✅ done |
| **Sandbox v1** | Declarative 2D **sandbox** — drag-drop actors, data-only behaviors (mover/spinner/bouncer/lifetime/spawner) + an `OnOverlap` event→action rule on the generic ECS, deterministic `tick`, Play/Stop via scene snapshot, F5/F9 save/load (`--sandbox`) | ✅ done |
| **Textured Sprites v1** | The Mini-Studio join — sandbox actors wear Texture Lab `.hrt` textures: a texture name on the pure `Sprite`, a nearest-neighbour `blit_scaled` primitive, cross-platform collection probe, inspector `Tex:` cycle; round-trips through save/snapshot | ✅ done |
| **Map / Level Lab v1** | Mini Studio — tile-grid **level editor** (`--maplab`): paint/flood-fill on the shared `fps::Map`, `fpsmap1` text format, palette + save/load collection; `--fps` loads the authored level (`maps/level_00.map`) with a default fallback | ✅ done |
| **Map Lab Spawn v1** | Mini Studio — levels own the **player start**: `fpsmap1` gains an optional `spawn CX CY DIR` line (backward compatible), the Lab adds a **Spawn** tool + Facing cycle + on-grid marker, and `--fps` reads the authored start instead of a hard-coded (3.5,8.5) — fixes the spawn-in-a-wall trap | ✅ done |
| **Particle System v1** | Engine depth (Track A) — reusable **CPU particle sim** (`particles_core`): deterministic seeded xorshift, fractional-accumulator emission, gravity, swap-pop reap, bounded pool, fade helpers; interactive `--fx` playground (fountain + click-bursts + live sliders) | ✅ done |
| **Textured Walls v1** | Mini-Studio 3-tool join — `--fps` skins wall ids 1–3 with Texture Lab `textures/wall_N.hrt` (procedural fallback); a Lab-authored level rendered with Lab-authored walls, joined through `assets/` by naming convention | ✅ done |
| **BaaS Asset Registry v1** | Lean BaaS (Track C) — project-scoped `/v1/assets` (upload/list/download/delete the Mini-Studio `.hrt`/`.map`): `assets` table, `web::asset` service + Drogon controller (api-key gate only), `?kind=` filter, optimistic `If-Match`, SDK `Assets` handle; the Track B→C bridge | ✅ done |
| **BaaS Test-Runner v1** | Lean BaaS (Track C) — managed headless runs: `/v1/testruns` job coordinator (create/claim/complete, atomic conditional-UPDATE state machine) + a `demo --runner` worker that runs `sandbox1` scenarios deterministically and posts pass/fail; SDK `TestRuns` handle; full loop tested end-to-end | ✅ done |
| **Tween & Easing v1** | Engine depth (Track A) — the animation interpolation primitive (`tween_core`): 13 named easing curves (`ease()`, clamped, endpoint-pinned; Back/Elastic overshoot) + a deterministic scalar `Tween` (one-shot + no-drift ping-pong); `--fx` "sweep" drives the emitter along a `SineInOut` curve | ✅ done |
| **2D Lighting v1** | Engine depth (Track A) — additive radial lights (`light_core`): smooth `(1-(d/r)²)²` falloff + `light_sample` folding intensity·falloff into an additive-weight alpha, a new `Renderer2D::add_pixel` (saturating glow primitive), `--light` dark-room demo (warm/cool/mouse lights, cool one drifts via a tween) | ✅ done |
| **Audio Mixer v1** | Engine depth (Track A) — pure software voice `Mixer` (`audio_core`): sums overlapping voices into a clipped int16 chunk (fixes the queue-only seam where clips couldn't play together), `tone` sine synth, streamed each fixed step via the existing `play_sound` (no platform edits); `--audio` demo (tone/chord buttons + live waveform) | ✅ done |
| **Sprite Animation v1** | Engine depth (Track A) — `anim::Flipbook` frame-index player (in `tween_core`): steps a sheet at `fps`, loops (bounded `t`, no drift) or holds one-shot. Vertical sheet packing → each frame is a contiguous `gfx::Sprite` drawn by the existing `blit_scaled` (zero new renderer code); `--anim` demo plays a generated 8-frame spinner (`sprites/spin_8.hrt`) with fps/loop/restart | ✅ done |
| **Sandbox Animated Actors v1** | Track A×B join — sandbox actors carry a sheet animation: `frames`/`fps` on the shared `Sprite`/`Archetype` (emitted by the `sandbox1` codec only when animated), drawn by a scene-wide cosmetic `anim_time_` clock, authored by cycling **Tex** to a sheet with an `anim fps` slider; round-trips through save/snapshot | ✅ done |
| **Studio Sheet Export v1** | Track B — the Texture Lab exports **animated sheets**: `make_sheet` turns any tileable recipe into an N-frame seamless-scroll loop (`sprites/sheet_NN.hrt`); frame count self-describes via aspect ratio (`anim::frames_in_sheet` = h/w), so `--anim` and `--sandbox` auto-discover Studio output — the animation pipeline is now self-hosting (no script) | ✅ done |
| **Project Manifest v1** | Platform spine (Horizon 0, [strategy](docs/strategy/)) — a versioned `game.project` manifest (`project_core`: parse/validate/round-trip, pure & headless) + a shared `launch_entry` seam so `--project <path>` launches a game **from a file** instead of a hard-coded flag; `--project-inspect` is the headless validate/doctor. `assets/projects/creator.gameproject` runs the FPS game with no `src/main.cpp` edit — the first step of the create→…→run golden path | ✅ done |
| **Resource Closure v1** | Platform spine (Horizon 0) — a manifest **declares its content** (additive `asset <type> <path>` lines) and the launcher enforces **dependency closure**: `resource_core` hand-written FNV-1a `content_hash` (deterministic, wasm-safe), `--project-inspect` reports each asset + hash, and `--project` **refuses to launch** on a missing dependency (hard reject). The seed of the package/preview-parity fingerprint | ✅ done |
| **Package Manifest v1** | Platform spine (Horizon 0→1 bridge) — `--project-package` emits a deterministic `package1` manifest: identity + content-hashed resources **sorted by path** + a combined `packagehash` (`resource_core::build_package`/`package_hash`). Order-independent, content-sensitive — the **immutable-release-id seed** a preview/rollback compares by hash. `docs/book/92` | ✅ done |
| **Create Verb v1** | Platform spine — the golden path's **create** step (first verb of the canonical *new → create → test → publish → run* loop): `--project-new <out-path> <entry> [name]` scaffolds a new **valid, launchable** `game.project` (reuses `project_core::to_text`, validates before writing, refuses to clobber). No hand-remembering the manifest format; the CLI golden path is now closed front-to-back. (The rich create UX remains the deferred Studio shell.) `docs/book/90` | ✅ done |
| **Release Store v1** | Platform spine (Horizon 1) — a **local immutable release store**: `--project-publish` writes the package manifest content-addressed at `releases/<hash>/` and points a **channel** (`development`/`preview`/`production`) at it; `--release-promote`/`--release-rollback` move channel pointers; `--release-status` reports them; `--project-verify <path> <channel>` is **preview parity** (metric P2 — "is what I'd ship == what's live?", exit 0/2/1 = match/drift/error). `release_core` = pure paths + channel format + trust-boundary validators (rollback args become paths); publish is idempotent (re-publish = *verified*) and **refuses to overwrite** a release id with different bytes. Completes the create→…→publish→promote/rollback→verify spine. `docs/book/93` | ✅ done |
| **Release Ops Hardening** | Platform spine (Horizon 1, exit gates 3/5/7) — publishes are now **atomic** (stage `.tmp` → rename, so a crash never exposes a partial release), **audited** (every publish/promote/rollback appends to an append-only `releases/audit.log` with timestamp, **predecessor**, and free-text reason — `--release-log [channel]` reads it forward, no directory scan), and channelled through **development → preview → production** semantics (publish defaults to `development`). The recorded predecessor means a bad release always has a known-good id to roll back to. New seam primitives `assets::append_file`/`assets::rename`. `docs/book/94` | ✅ done |

## Prerequisites (macOS)

```sh
brew install cmake sdl2
```

(Apple clang or Homebrew clang for a C++20 compiler — already present on most Macs.)

## Build & run

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/demo --project-new projects/mine.gameproject fps "My Game"  # headless: scaffold a new launchable manifest (create verb)
./build/demo --project projects/creator.gameproject   # golden path: launch a game from a game.project manifest
./build/demo --project-inspect projects/creator.gameproject  # headless: validate/doctor a manifest (no window)
./build/demo --project-package projects/creator.gameproject  # headless: emit the deterministic package manifest (release-id seed)
./build/demo --project-publish projects/creator.gameproject development "reason"  # headless: store immutably (atomic) + point the development channel + audit
./build/demo --release-status                                # headless: what development/preview/production point at (+ present?)
./build/demo --release-promote development preview "reason"  # headless: promote dev→preview (atomic, audited); then preview→production
./build/demo --release-rollback production <release-id> "reason"  # headless: aim a channel back at a prior release (audited)
./build/demo --release-log [channel]                        # headless: append-only audit history (publish/promote/rollback, w/ predecessor + reason)
./build/demo --project-verify projects/creator.gameproject development  # headless: preview parity (P2) — exit 0 match / 2 drift / 1 error
./build/demo            # M0 engine demo
./build/demo --gui      # chess (GUI)     — also: hvh|hvai  easy|medium|hard
./build/demo --tui      # chess (terminal)
./build/demo --fps      # M2 raycaster
./build/demo --3d       # M3 real 3D core (cube + sphere + grid; drag/WASD/ENTER/SPACE/RMB)
./build/demo --viz3d    # M3.5 interactive sandbox (1-4 spawn, click select, drag move, ...)
./build/demo --iso      # M4 isometric farm sim (1-0 brushes, LMB paint, RMB walk farmer, F5/F9 save/load)
./build/demo --editor   # F editor: immediate-mode GUI + physics sandbox (click to drop bodies)
./build/demo --colony   # integration: iso agent sim on ECS + jobs + frame alloc + asset cache + GUI
./build/demo --studio   # Mini Studio: procedural Texture Lab (Save → .hrt; Export Sheet → animated sprites/sheet_NN.hrt)
./build/demo --sandbox  # declarative sandbox: drag-drop actors, attach behaviors + textures (+ animated sheets), Play/Stop (F5/F9)
./build/demo --maplab   # Map/Level Lab: paint/flood-fill a tile grid + place the player Spawn, Save → maps/level_NN.map (loaded by --fps)
./build/demo --fx       # particle playground: fountain + click bursts + live sliders + "sweep" (tween-driven emitter)
./build/demo --light    # 2D lighting: dark room, additive radial lights; mouse light follows, sliders for radius/intensity
./build/demo --audio    # audio mixer: tone buttons + "chord" (sums 4 voices) + master volume + live waveform
./build/demo --anim     # sprite animation: plays an 8-frame spinner sheet via a Flipbook; fps/loop/restart controls
./build/demo --runner <baas_url> <api_key>   # headless test-run worker: polls the BaaS, runs sandbox scenarios
ctest --test-dir build --output-on-failure   # unit tests (math, render3d, viz3d, chess, fps, iso, studio)
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
# open http://localhost:8765/demo.html            # default scene
# open http://localhost:8765/demo.html?mode=project  # golden path: FPS from the game.project manifest
```

The web build runs the **same** engine/game code; only the platform `run()` loop is
`#ifdef`'d to `emscripten_set_main_loop`. Pick the scene with `?mode=…` (see
`argsByMode` in [`web/shell.html`](web/shell.html)); `?mode=project` launches the same
`projects/creator.gameproject` the native `--project` path runs, so web and native
select the scene identically.

### Native webserver (requirements §11 — optional, separate process)

A hand-written C++ HTTP server (no framework, POSIX sockets) that serves the WASM
build and a tiny leaderboard API. It links **no** engine code — the game and server
meet only over HTTP.

```sh
cmake --build build --target webserver
./build/webserver --root build-web        # → http://localhost:8080/  (the game)
# API: GET/POST http://localhost:8080/api/scores  ({"name":"..","score":N})
```

### Game BaaS — Slice #1 (auth + leaderboard, separate process)

A Drogon-based backend (`baas/`) with accounts (argon2id passwords + HS256 JWT) and a
multi-tenant leaderboard, plus a unified **non-blocking C++ SDK** (`sdk/cpp/`) the
colony game uses — native (libcurl) and web (emscripten_fetch). The backend links
**no** engine code; the engine core gains **no** dependency (only the SDK does).

```sh
brew install drogon libsodium                          # one-time backend deps
cmake --build build --target baas
./build/baas --db sqlite://baas.db --seed              # create the demo project (prints keys)
BAAS_JWT_SECRET=change-me BAAS_ADMIN_SECRET=admin-me ./build/baas --db sqlite://baas.db --static build-web
# native:     ./build/demo --colony         (with baas running on :8080)
# web:        http://localhost:8080/demo.html?mode=colony
# dashboard:  http://localhost:8080/dashboard   (operator UI; admin secret + the seeded sk_ key)
```

See guidebook chapters 51–58 for the design.

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
baas/           Game BaaS backend (Drogon) — gateway + auth + leaderboard; separate process
sdk/cpp/        unified non-blocking C++ SDK (gbaas) — native (libcurl) + web (emscripten_fetch)
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
