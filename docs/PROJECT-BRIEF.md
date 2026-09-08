# Project Brief — orientation for an AI agent

**Audience:** an AI coding agent (or a new human maintainer) that must understand this
repository well enough to *choose the right next piece of work*, not merely to edit a file
it was pointed at.
**Last verified:** 2026-09-06, against the chapter-133 merge (`main`).
**Language note:** written in English to match the rest of `docs/` and the code; the
original vision document `requirements.md` is in Vietnamese and remains authoritative for
the learning goal.

---

## 0. How to use this document, and what owns what

This brief is a **map and a decision aid**. It deliberately does not restate what another
file already owns. When this brief and a source-of-truth file disagree, the source of
truth wins and this brief is stale — fix it.

| File | Owns | Read it when |
|---|---|---|
| `requirements.md` (Vietnamese) | The original **learning** vision and the hard constraints | You are tempted to add a dependency or a shortcut |
| `CLAUDE.md` | **Operating instructions** for an agent: commands, invariants, build patterns | Before any build/test/edit |
| `docs/strategy/` (6 docs) | The **product** direction: market, gap analysis, target architecture, roadmap, metrics, competitors | Before choosing what to build next |
| `docs/book/` (148 chapters) | The **explanation** of every subsystem, one chapter per feature | Before modifying a subsystem you did not write |
| `docs/guides/` | Operator runbooks (`author-to-url.md`, `backup-restore-drill.md`) | Before touching release or backup flows |
| `docs/superpowers/{specs,plans}` | Per-milestone design specs and execution plans | To see how a feature was originally scoped |
| **This file** | The **synthesis**: current state, feature inventory, status ledger, decision rules | At the start of a session, or when picking work |

**Reading order for a cold start:** this file → `CLAUDE.md` → the `docs/book/` chapter for
whatever you are about to touch.

---

## 1. What this project is

Two goals live in this repository at once. Confusing them is the single most common way to
choose the wrong work.

**Goal A — learning (original, still primary).** Build a game engine *by hand* in C++20 to
understand every layer deeply: math, software rasterization, ECS, allocators, job systems,
physics, networking, backend services. When "convenient" and "teaches more" conflict, the
project chooses *teaches more* — as long as something runnable ships.

**Goal B — product (adopted 2026-07-11).** Grow the accumulated subsystems into **one
inspectable, self-hostable game-creation platform** whose canonical loop is:

> **new project → create → test → publish → operate → learn**

The adopted posture that reconciles them is called **_blend_** (`docs/strategy/04`, §"Adopted
execution posture"): *one thin golden path first, then interleave*. Every cycle pairs one
plumbing/golden-path slice with one hand-written runtime or learning slice, so product work
never fully crowds out the deep-dive work that keeps a solo project alive.

**Capacity assumption:** one primary maintainer. One vertical slice at a time. Horizons are
planning windows, **not delivery promises** — a horizon ends when its outcome is
demonstrated, never when its calendar expires.

---

## 2. Non-negotiable constraints

These are the rules the whole design exists to protect. Breaking one is a **design
regression**, not a shortcut, and an agent must refuse to break them silently.

1. **SDL2 is the only runtime dependency, and only as a thin shim** — window, raw
   framebuffer present, raw input, audio, timing. Never `SDL_Renderer`, `SDL_image`, or any
   SDL drawing primitive. Every pixel is drawn by our own code into a CPU framebuffer.
2. **Engine/game code never includes `<SDL.h>`.** It talks only to `src/platform/platform.hpp`.
   If an SDL type appears in a header above the platform layer, the abstraction is leaking.
3. **No blocking `while(true)` loop above the platform layer.** A frame is one
   `App::frame(dt)` call driven by `platform::run()`. This is exactly what lets the web build
   substitute `emscripten_set_main_loop` with zero engine/game changes.
4. **All file I/O goes through `assets::`** (`src/engine/assets.*`), never scattered `fopen`.
   The web build uses a virtual filesystem; the asset root is the origin of every runtime path.
5. **Web portability is baked in, not bolted on.** The same engine/game code compiles native
   and WASM; only `run()` is `#ifdef`'d.
6. **The BaaS is a separate process and links no engine code.** The engine core gains no
   dependency from `baas/` or `sdk/`. `baas` is CMake-guarded on Drogon being installed.
7. **Every GUI action must have a reproducible command/domain equivalent.** A window may
   never be the only way to perform an operation (roadmap rule 3).
8. **Operations live in pure `*_core` libraries; triggers only call them.** A CLI flag and a
   keypress must invoke the *same tested function*, never two implementations. This is why
   `--hub` and the Studio's Hub section cannot drift.
9. **`CMakeLists.txt` lists sources explicitly** — no globbing, on purpose.
10. **A "production" claim requires evidence**: migration, security, observability,
    backup/restore, rollback, and named operating ownership (roadmap rule 4).

---

## 3. Scale and shape of the repository

| Measure | Value |
|---|---|
| Commits on `main` | 431 (measured 2026-09-07) |
| C/C++ source | ~31,000 lines (`src/` 25,788 · `baas/` 5,276 · rest `sdk/`, `server/`, `tests/`) |
| Test suites | **95, all passing** (~39 s) — headless, no window. A build without Drogon registers **60**, measured; the 35 Drogon-gated ones run in their own CI job since chapter 129, and every one of them now carries `TIMEOUT 120` (chapter 140: a self-deadlock cost 25 minutes of silence before ctest's default noticed). |
| SDL-free static libraries | 36, each with a matching `test_*` target |
| CLI modes in one `demo` binary | `--help` prints them; `--lab` lists the labs, `--cmd` lists the commands |
| BaaS HTTP routes | 45, plus `/v1/ws`, `/metrics`, `/healthz`, `/dashboard` |
| Guidebook chapters | 148 (`docs/book/00`–`147`) |
| Strategy documents | 6 (`docs/strategy/01`–`06`) |
| CI | GitHub Actions: `build-test` (ubuntu + macOS, clean checkout + golden-path smoke, and since chapter 138 it **re-plays a battle recorded on the other architecture**), `web-build` (emscripten, and it **runs** the page — a real touch drives the farm and the creature game, Play on the collection page reaches a running game, and the wasm build re-plays the same recorded battle), `baas-test` (27 Drogon tests in `drogonframework/drogon`) |

### Repository layout

```
src/platform/    the fixed platform seam (platform.hpp) + backend_sdl.cpp
src/engine/      hand-written core: math, renderer2d, renderer3d, geometry, camera,
                 assets, image, text, ui, ecs/, jobs/, memory/, physics/, anim/, fx/,
                 audio/  +  the platform spine: project/ (manifest + inspect),
                 resource/, release/, hub/, commands/, document/, tilemap/
src/games/       one directory per scene/tool (chess, fps, viz3d, iso, editor, colony,
                 studio, sandbox, hub, studio_shell, farm, fx, light, audio,
                 anim, runner) — studio_shell holds the Workspace interface + its two
                 implementations and the full-screen WorkspaceHost that --lab scene uses
src/main.cpp     mode dispatch + the launch_entry seam
tests/           77 dependency-free suites (49 without Drogon — see the BaaS row in §8)
baas/            Drogon Game-BaaS backend (separate process, links no engine code)
sdk/cpp/         gbaas C++ SDK — native libcurl / web emscripten_fetch, one API
server/          hand-written HTTP server (POSIX sockets), serves the WASM build
web/shell.html   the Emscripten shell; ?mode= selects the scene without recompiling
docs/            book/ (148 chapters), adr/ (the decision index), strategy/ (6), guides/ (2), superpowers/{specs,plans}
assets/          the asset root: fonts, maps, sprites, textures, projects/, releases/, channels/
```

---

## 4. The four blocks of the system

### Block 1 — Engine core (`src/engine/`)

Hand-written, SDL-free, unit-tested headless. Contents:

- **math** — vec2/3/4, mat4, trig helpers
- **renderer2d** — CPU framebuffer: pixel, line, rect, sprite blit, `blit_scaled`,
  anti-aliasing, `add_pixel` (saturating additive glow for lighting)
- **renderer3d** — software rasterizer: triangles, z-buffer, perspective, backface culling,
  flat/Gouraud/wireframe shading
- **geometry** — mesh, primitive generation (cube/sphere/plane/grid), transforms
- **camera** — orbit camera (visualization) + free/FPS camera
- **assets** — the I/O seam: `load_file`, `write_file`, `append_file`, `rename` (atomic)
- **asset_cache** — per-type loaders, hot reload on file change
- **image / text** — image decode; stb_truetype font rendering with AA
- **ui** — hand-written immediate-mode GUI
- **ecs** — generic type-erased sparse-set registry, generation-safe handles, views
- **jobs** — thread pool + counters + `parallel_for` (multicore native, synchronous on web)
- **memory** — five allocators: arena, stack, pool, freelist, frame
- **physics** — 2D rigid bodies, circle/box collision, impulse resolution + positional correction
- **anim** — `Flipbook` frame-index player; `frames_in_sheet` self-describes via aspect ratio
- **fx** — deterministic seeded particle simulation (xorshift, fractional-accumulator emission)
- **audio** — pure software voice `Mixer` summing overlapping voices into clipped int16
- **tween** — 13 named easing curves + deterministic scalar tween (one-shot + ping-pong)
- **light** — additive radial 2D lights with `(1-(d/r)²)²` falloff
- **project / resource / release / hub** — the platform spine (see §6)

### Block 2 — Games and Labs (`src/games/`)

| Mode | What it is |
|---|---|
| *(default)* | M0 engine demo, retro 480×270 framebuffer |
| `--gui` / `--tui` | Chess, GUI and terminal, human-vs-human or minimax/alpha-beta AI at 3 depths |
| `--fps` | Wolfenstein-style raycaster: textured walls, billboard sprites, audio; loads a Map-Lab-authored level |
| `--project <manifest>` | **A game**, launched from its `game.project` — `projects/creator.gameproject` (entry `fps`) and `projects/farm.gameproject` (entry `farm`) |
| `--lab 3d` | Software-rasterized 3D core: cube, sphere, grid, shading modes |
| `--lab viz3d` | Interactive 3D sandbox: spawn, mouse-pick select, drag-transform, orbit/pan/zoom |
| `--lab iso` | Isometric farm sim: tile map, depth sort, small ECS, A* pathfinding, save/load |
| `--lab editor` | Immediate-mode GUI + physics sandbox |
| `--lab colony` | **Integration game** standing on the whole engine core (ECS + jobs + frame allocator + asset cache + GUI) and on the BaaS via the SDK |
| `--lab texture` | **Texture Lab**: hand-written seamless noise (value/Perlin/fBm), `.hrt` export with re-editable recipes, animated sheet export |
| `--lab scene` | The Studio's Scene workspace, full-screen — the same object its Scene tab holds |
| `--lab mixer` | The Studio's Mixer workspace, full-screen — a sprite ASSEMBLED from parts of a sheet plus palette swaps. Save writes the `.mix` (source), Bake writes the `.hrt` |
| `--lab map` | The Studio's Map workspace, full-screen — the SAME object as its Map tab: layers, paint/rect/fill, the Entity tool that places a spawn, and the Rule button (none/line/blob per material, drawn as connectivity because there is no tileset renderer here). `map2` only |
| ~~`--lab fx` `light` `audio` `anim`~~ | **Retired, chapter 133** — particles, 2D lighting, the mixer and sprite animation are now `Emitter`/`Light`/`Sound`/flipbook components on an actor, edited in `--lab scene`'s EFFECTS section and saved in the scene file |
| `--hub` `--shell` | The platform surfaces (see §6) |
| `--runner` | Headless BaaS test-run worker |

Thirteen labs became nine in chapter 133. Twelve of them used to be twelve top-level flags, and `--hub-ui` was a second
Hub window. Chapter 120 folded them: a **game** is launched from a manifest, the
**Studio** is one flag, and everything else windowed is a **lab** behind one door
derived from one table. `demo --help` prints the whole surface and where each retired
flag went; an unknown `--flag` is an error carrying that list, not a different window.

**The Labs are joined by naming convention through `assets/`**, not by an import system:
Texture Lab writes `textures/wall_N.hrt`, and `--fps` skins wall ids 1–3 with them.

### Block 3 — Platform spine (`src/engine/{project,resource,release,hub}`)

The create→publish→operate machinery. Fully described in §6.

### Block 4 — Game BaaS (`baas/`) + SDK (`sdk/cpp/`)

A hand-written Drogon backend, multi-tenant by project, running as a separate process.

| Service | Capability |
|---|---|
| auth | argon2id + JWT, register/login/guest |
| leaderboard | per-key top/me, score submit |
| cloud save | versioned per-user/per-slot state, optimistic concurrency (`If-Match`) |
| inventory | per-user item quantities, grant/consume, server-enforced non-negative spend |
| store | priced catalog, buy-by-SKU, **atomic purchase** (spend + grant in one transaction) |
| remote config | client read + admin write |
| analytics | event ingest + admin summary + **per-release attribution** |
| live events | scheduled events, reversible audited config change |
| realtime | WebSocket `/v1/ws`: lobby, matchmaking, tenant-scoped rooms, auth-on-upgrade |
| replays | per-user immutable named recordings, 512 KiB cap |
| asset registry | project-scoped `/v1/assets` for `.hrt`/`.map`, optimistic `If-Match` |
| test runner | `/v1/testruns` job coordinator with an atomic conditional-UPDATE state machine |
| admin/dashboard | two-level auth (platform `X-Admin-Secret` + per-project `X-Secret-Key`), hand-written web dashboard |
| RBAC | operator identity, project roles, project secret rotation |
| hardening | token-bucket rate limiter, idempotency keys, request correlation IDs, `/metrics`, structured access log, versioned migrations, audit log |

**SDK:** one non-blocking C++ API over two transports — native libcurl and web
`emscripten_fetch` — behind `ITransport`/`IWsTransport`. The colony game uses it identically
on desktop and in the browser.

---

## 5. Build, run, test, deploy

```sh
# native
brew install cmake sdl2
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
ctest --test-dir build --output-on-failure          # 80 suites, headless
ctest --test-dir build -R chess                      # one suite

# sanitizers
cmake -B build-asan -DENGINE_SANITIZE=ON && cmake --build build-asan

# web
source ~/emsdk/emsdk_env.sh
emcmake cmake -B build-web && cmake --build build-web --target demo
./build/webserver --root build-web --port 8080       # the project's own hand-written server
# then http://127.0.0.1:8080/demo.html?mode=shell   (?mode= picks the scene, no rebuild)

# backend (guarded on Drogon; absent Drogon its targets vanish from ctest silently)
brew install drogon libsodium
cp baas/config.example.json baas/config.json
./build/baas/baas
docker compose -f baas/ops/docker-compose.yml up --build

# backend-only build with no SDL2 present (this is how the container builds)
cmake -B build-server -DENGINE_BUILD_DESKTOP=OFF
```

**CI** (`.github/workflows/ci.yml`) does a clean checkout on ubuntu + macOS, builds, runs
`ctest`, then smoke-tests the entire golden path (create → inspect → publish → verify →
promote → status → log → hub) and asserts no `.tmp` file leaks from the atomic publish.
**Breaking a headless verb breaks CI even when `ctest` is green.**

---

## 6. The platform spine — the part most easily broken

This is the data flow an agent must understand before touching `src/engine/project|resource|release|hub`
or `src/games/hub|studio_shell`.

```
game.project manifest              (project_core)   identity, entry, asset declarations
        │
        ├── resource closure       (resource_core)  FNV-1a content_hash per declared asset
        │                                           launch HARD-REFUSES on a missing dependency
        │
        ├── package manifest       (resource_core)  resources sorted by path + combined
        │                                           packagehash → order-independent,
        │                                           content-sensitive → this IS the release id
        │
        ├── release store          (release_core)   releases/<hash>/ immutable, content-addressed
        │   channels                                development → preview → production are pointers
        │   ops                    (release_ops_core) publish / promote / rollback → OpResult
        │
        └── hub                    (hub_core)       hub_lines() + recommend() → one next action
```

**Properties that must be preserved:**

- **Atomic publish** — stage to `.tmp`, then `assets::rename`. A crash never exposes a
  partial release.
- **Audited** — every publish/promote/rollback appends to an append-only
  `releases/audit.log` with timestamp, **predecessor**, and free-text reason. The recorded
  predecessor is what makes a bad release always have a known-good id to roll back to.
- **Idempotent** — re-publishing identical bytes is a *verified no-op*; publishing different
  bytes under an existing release id is **refused**.
- **Preview parity (metric P2)** — `--project-verify <proj> <channel>` answers "is what I
  would ship the same as what is live?" with exit code 0 = match, 2 = drift, 1 = error.
- **One view model** — `hub_core::hub_lines`/`recommend` is shared by the headless `--hub`
  and the Studio's Hub section, so CLI and window cannot drift.
- **`launch_entry` seam** — a manifest names an entry (`fps`), and `src/main.cpp` maps entry
  names to scenes. **A new reference game must not require a new CLI flag**; extend
  `launch_entry` and `kKnownEntries` instead.

**Verified working end to end** (2026-09-04): all three channels point at release
`c95febd882741b29`, driven interactively from the Studio shell; `assets/releases/audit.log`
records the publish and both promotions.

---

## 7. Work completed

### Engine milestones (M0–M5, A–F, integration) — all done

M0 engine foundation · M1 chess GUI+TUI+AI · M2 FPS raycaster · M3 real 3D core ·
M3.5 interactive 3D sandbox · M4 isometric sim · **M5 WebAssembly port with no engine/game
rewrite** · §11 hand-written webserver · A memory allocators · B ECS · C job system ·
D asset pipeline + hot reload · E 2D physics · F editor UI · colony integration game.

### BaaS slices S1–S10 — all done

Auth+leaderboard · cloud save · inventory · remote config/analytics/live events ·
dashboard with two-level admin auth · realtime lobby+matchmaking · realtime proven live in
the game (presence panel, native + web) · replay system · rate limiter · observability
(`/metrics`, structured access log).

### Mini Studio and Track A runtime depth — all done

Texture Lab · sandbox editor · Map Lab (+ author-placed spawn) · textured sprites ·
textured walls · particles · tween/easing · 2D lighting · audio mixer · sprite animation ·
sandbox animated actors · self-hosting sheet export · BaaS asset registry · BaaS test runner.

> **Important caveat, recorded in `docs/strategy/02` §10b:** most of this Track A depth was
> built *ahead of a golden-path consumer* — each is a proven learning implementation with
> tests and a chapter, but each remained a separate `--flag` scene rather than something
> reachable through a versioned project. This is precisely the "breadth before product"
> risk. An agent should not add a *thirteenth* isolated subsystem on this pattern.

### Horizon 0 → 1: the platform spine — done

Project manifest and golden path (ch. 90) · resource identity and closure (91) ·
package manifest (92) · immutable release store (93) · atomic/audited releases (94) ·
Hub shell and recommended action (95) · graphical Hub scene (96) · interactive Hub +
Studio shell over extracted, tested release ops (97) · clean-checkout CI ·
`docs/guides/author-to-url.md` operator guide with a failure lab.

### Horizon 2 in progress — ten slices landed

| Slice | Chapter | What it added |
|---|---|---|
| Production persistence | 98 | Versioned migrations + audit log + **executed** backup/restore drill |
| Reversible LiveOps | 99 | An audited remote-config change that can be reverted |
| Release measurement | 100 | Per-release analytics attribution |
| Secret rotation | 101 | Project secret rotation |
| Idempotency | 102 | Retry-safe inventory grant, key scoped by (user, item) |
| Atomic purchase | 103 | Spend currency + grant item in one transaction |
| Correlation IDs | 104 | Request tracing on every response (telemetry) |
| Store catalog | 105 | Priced offers, buy-by-SKU (economy) |
| RBAC | 106 | Operator identity + project roles |
| Deployment | 107 | Backend container + `ENGINE_BUILD_DESKTOP=OFF` build split |

### ...and forty chapters after them (108–147), by theme

The table above stops at 107 because that is where the slices were still one-per-concern.
What followed was the PLAN-v2 correction roadmap (S19–S30c) plus two slices beyond it, and
it is more honestly read by theme than by row:

| Theme | Chapters | What changed |
|---|---|---|
| **Studio and UI** | 108–116, 120, 127, 132–133, 144, 147 | UTF-8 + a real design system · `ui` v2 (clip, overlays, focus, layout) · workspaces as tabs *and* full-screen labs · one door per kind (12 flags → `--lab`) · a draggable split with a persisted layout · a status strip of cells · `xy_pad` on one shared drag loop |
| **Content formats** | 110, 131, 134–135 | `map2` is the only format anything writes · autotile rules live in the MAP · provenance DERIVED from the marks four doors leave · the fourth door (`asset.mix`) |
| **Games** | 113, 117, 124–126, 136–138, 143, 146 | The farm (day loop, NPCs, cloud save, seasons as a RULE) · Creatures (18 species, integer battle, replay as a file, rated PvP a hand can play) |
| **Web** | 118, 123, 128, 130 | The page runs in CI, driven by a real finger · the collection page a stranger can open |
| **Backend** | 129, 139–142 | CI runs the BaaS suite · Elo shared header · every read-then-write locked · **Postgres actually works** (30/30, CI runs both backends) · the API describes itself |
| **The spine** | 145 | The release id names the RELEASE, not just its bytes — two asset-less games used to collide |
| **Bookkeeping** | 143, 145 | `docs/adr/` decision index · `bench_core` (the games' frame cost, measured for the first time) |

**Where that leaves Horizon 2:** the golden path, the Studio, five games, a backend on
Postgres and a self-describing API are done. §9 below is what is left, and the largest
untouched piece is **segments and experiments**.

---

## 8. Verification ledger — what is proven, what is only written

An agent must not upgrade any of these from "written" to "works" without running it.

| Claim | Status |
|---|---|
| 95 test suites pass | ✅ **verified** 2026-09-08, twice in a row on purpose — chapter 140's `db_url` helper appended `.db` to a name that already had one, so the suite passed once against fresh files and failed in ten places on the second run against databases nothing had emptied. A green suite is not evidence unless it is green twice. (Chapter 140 added `test_baas_dialect` and `test_baas_concurrency`; chapter 139 added `test_elo`, `test_netbattle` and `test_creature_pvp_live`; chapter 138 added no suite — its checks went into `test_creature` and `test_creatures_scene`, where they belong; chapter 137 added `test_creature_world` and `test_creatures_scene`; 81 at 136 with `test_creature`; 80 at 135 with the fourth `.hrt` door, 78 at 134, 77 at 132, 62 at 108). A build without Drogon registers 60, measured. |
| Native build | ✅ verified |
| The BaaS suite runs somewhere other than a laptop | ✅ verified 2026-09-06 (chapter 129) — and ❌ **before it, since the day those tests were written**. `ctest` here reports 76; CI reported **48**. The 28 in between were the Drogon-gated ones — auth, JWT, RBAC, purchases, cloud saves, inventory, secret rotation, idempotency, and the three end-to-end runs that boot a real server and drive it through the real SDK — skipped because the runner only ever installed SDL2. Measured, not counted: configuring with `-DCMAKE_DISABLE_FIND_PACKAGE_Drogon=ON` reproduces exactly what CI sees. (The planning note had said "23", which was the number of `test_baas_*.cc` **files**; a file with a `foreach` registers seven tests and a helper registers none.) The job is `baas/ops/Dockerfile`'s build stage stopping at the tests, and it carries no list: `BUILDSYSTEM_TARGETS` gives a `baas_tests` target and `ctest --test-dir build/baas` selects exactly that directory's tests — two of which are named `metrics` and `rate_limiter`, so no `baas_*` regex would have found them. **Verified by running it** in `drogonframework/drogon:latest` on the runner's own architecture before the YAML was pushed: 27/27 in 51 s. ⚠️ **making CI run them found a real bug in the first minute**: `test_sdk_realtime_live` does not COMPILE against libcurl 7.81 (it calls `curl_ws_recv`, added in 7.86; Ubuntu 22.04 is the Drogon image's base), and a test that will not compile does not fail one test — it fails the build and takes the other 27 with it. The asymmetry is the point: `sdk/cpp` already degrades to an inert realtime stub and says so, while the test that exercises the degradation was added unconditionally, and the only machine that ever built this half was a Mac with Homebrew's curl 8.x. Now gated on the same two variables as the transport, with a configure-time message, because a test that vanishes quietly is the failure this slice exists to end. The job also asserts a floor of 27 registered tests — a green job that ran nothing is otherwise indistinguishable from a green job that ran everything. ❌ **the SDK's native ws:// realtime is a stub on any Ubuntu 22.04 build, including this project's own backend image**, silently at runtime; `sdk_realtime_live` therefore still runs on exactly one machine. ❌ no Docker job: CI builds and tests the backend, it does not build the image or probe `/healthz`. ✅ **since chapter 141 the job runs the same suite twice, once per backend** — `BAAS_TEST_DB` points every test at a `postgres:16-alpine` service and each one wipes the schema on the way in. Not a different suite and not a subset: a query that parses in SQLite and not in Postgres now fails in the test that already covers the feature. ⚠️ that second run has not yet been observed on a GitHub runner. |
| Web (Emscripten) build + served by the hand-written webserver | ✅ verified: build links, all assets return 200 |
| The web page a game ships on | ✅ verified 2026-09-06 (chapter 128). Until this it was a debug shell — mono font, an always-visible runtime log, the canvas capped at `78vh`, and **zero lines about touch** — while chapters 126 and 127 had made the farm playable by thumb and the Studio able to author. Now: one 28 px bar, the canvas fitted to the stage, `100dvh` (not `100vh`, which on a phone is the height with the URL bar *hidden*), `viewport-fit=cover` + `env(safe-area-inset-*)`, a fullscreen button on the stage, and the log behind a toggle. **Proved with a real finger, not a mouse**: `scripts/web_touch_check.mjs` drives Chrome over CDP with touch emulation, dispatches `Input.dispatchTouchEvent`, and reads a VALUE back out of the game — `var px` from the save the game itself wrote — rather than comparing pictures. Holding the east button walks the player `px` 4 → 8; aiming one button over reports `px 4 -> 1`, i.e. the finger arrived and walked WEST, which is the strongest evidence available that touch lands where the game says the button is. It aims by reading the game's own `farm: controls 640x360 right=116,206,44,44 …` line rather than recomputing the layout (a third copy of the ch.126 rule would be the first to go stale). CI now RUNS the page; before, "web build green" only ever meant the link succeeded. ⚠️ **the bug was one nobody was looking for**: Emscripten sets `canvas.style.width/height` inline, after which `max-width`/`max-height` clamp the two axes independently — the intrinsic-ratio rule stops applying — so a 1280×720 game was displayed 390×720 on a phone. Every assertion passed and touch landed correctly (SDL maps the pointer through the same wrong-shaped box); **only a screenshot showed it**, the third chapter running where that was true. Fixed by sizing from the drawing buffer (`fit()` + `ResizeObserver` + `MutationObserver`) and pinned by an aspect assertion. ⚠️ `touch-action: none` is **declared, not verified, and cannot be verified by this harness**: flipping it to `auto` still passes, and a probe found that CDP's synthetic touch ignores touch-action entirely (a drag scrolled a scrollable page by the same 110 px either way). ❌ still one finger; portrait is a stamp (390×219, so the 44-logical-px d-pad is 27 CSS px — the page says "rotate", it does not fix it); Chrome only, no Safari/iOS; `?shell=` on a phone untested; no Collection page **until chapter 130 (below)**. |
| Where every picture came from | ✅ verified 2026-09-06 (chapter 131) — and ❌ **before it, since chapter 122**. `CLAUDE.md` carried the rule ("every new `.hrt` gains a line in `assets/ATTRIBUTION.md` in the same change") with nothing enforcing it, and by the time it was measured it had been forgotten **twenty times out of twenty-three**: 23 `.hrt` files on disk, 3 named in that document, the rest covered by a closing paragraph saying everything else came from code here. Probably true; not checkable; the same shape of blanket assurance chapter 129 found under 28 tests that had never run. `provenance_core` derives the answer instead: the three doors each leave a different mark on disk (a `.pack` naming the import, a sibling `.recipe`, a sibling `.pix`), anything else is `UNRECORDED`, `Ledger::ok()` goes false and `test_provenance` goes red. The table inside `ATTRIBUTION.md` is now **generated** between two markers — prose above and below survives — and byte-compared by a test, the same relationship `.hrt` has to its `.recipe`. It needed `assets::list_tree`: `list_dir` walks one folder, and a ledger built on it would have reported a clean sheet while `pieces/`, `sprites/` and the asset root held **14 of the 20 holes**. ⚠️ **making it checkable also found the import door untested**: `.recipe` and `.pix` are each re-baked and byte-compared and always have been, but the import — the one door with a **licence** behind it — never was; `test_commands` now re-runs every `import` line in every `.pack` and compares bytes, reading the claims out of the packs rather than naming Kenney. ❌ **`declared` is a promise, not a bake**: twenty files have no surviving source and the ledger says so rather than fixing it. ❌ only `.hrt` is tracked — fonts (named in prose), maps, `.def` files and scenes have no ledger. ❌ nothing re-bakes the ledger on an import/texture/pixels bake; only `asset.new` does, and the test catches staleness afterwards. ❌ no `.pack` importer: a pack is hand-written, nothing downloads, unzips or checksums. |
| The Studio can add an asset, not only edit one | ✅ verified 2026-09-06 (chapter 131). Chapter 127 wrote the ceiling down: the Pixels workspace edits the textures a manifest **already declares**, so making one new tile meant a text editor, a bake command, a manifest line and an attribution line — four steps in three places. `--cmd asset.new <name> <tile-px> <cols> <rows> [<proj>]` and the Create button in the Pixels inspector do the whole act, and the button calls the command rather than a private copy of it. It writes the **`.pix` first** and bakes from it: a blank `.hrt` written straight to disk would arrive with no origin at all — the exact hole the row above refuses. ⚠️ **the first draft validated the manifest after writing the files**, so a bad project path reported failure and left two real files behind plus a stale ledger; everything refusable is now refused before anything is written. ⚠️ **writing the test found that no test in `test_pixel_workspace.cpp` had ever released a mouse button** — `ui::interact` activates on release-over, so the suite could drag a slider and type into a field and had never pressed a button; four tool buttons, Undo, Redo and Save were drawn in every frame of every test and clicked in none. Create is now driven end to end through the real field and the real rect, and Save is clicked. ❌ the button makes a **16 px, one-tile** sheet; the command takes any size. ❌ a new sheet opens with an **empty palette** (it is sampled from the image), so the mixer is the only way to pick a colour on it. ❌ the four tool buttons still publish no rects and are still never clicked. |
| The link you can actually send: a Collection page | ✅ verified 2026-09-06 (chapter 130). Chapter 128 made one game reachable at a URL nobody could guess. `web/collection.html` now lists every `*.gameproject` as a card — cover, one-line summary, entry, package hash, Play — and `scripts/web_collection_check.mjs` **taps Play and waits for the destination page to say `running`**, because a correct `href` is not a working link (chapter 123's keyboard was wired to a canvas nobody had focused, and every check that stopped one step short passed). The manifest gained optional `cover` and `summary`; `to_text` emits them **only when set**, which is the whole migration and is a test (`to_text(parse(old)) == old`). A `cover` joins the resource closure — it ships, so it is hashed — **unless the manifest already declared that path**: the farm's cover is its own tileset, and hashing one file twice would move the release id with no change in content (confirmed by publishing after the change and getting `verified`, not a new id). The index is **baked**, not hand-kept (`--cmd collection.index` + a new `assets::list_dir`, sorted because it feeds a committed file): `assets/collection.json` stands to the manifests as a `.hrt` does to its `.recipe`, and `test_collection` re-bakes and compares bytes **and** asserts the entry count equals `list_dir`'s, so adding a game and forgetting to re-index is a red test rather than a game missing from the page. The page **decodes `.hrt` itself** (15 lines of `DataView` — `"HRT1" | BE w | BE h | RGBA8`), so a cover is the same file the engine reads and **no fourth offline door into `.hrt` was opened**. ⚠️ **a screenshot found the bug again, the fourth chapter running**: every assertion passed on a page whose 320×180 canvas buffer was being resampled by CSS to 258×145 — the careful integer scale multiplied by 0.81 and thrown away. Fixed the same way chapter 128 was (size the buffer from the box it is shown in, plus a `ResizeObserver`) and pinned by a buffer-vs-box assertion. ⚠️ **a build-system trap sprung within the hour of the comment warning about it**: the copy into `build-web/` was a `POST_BUILD` step, which only runs when `demo` relinks, so editing the page copied nothing and the browser check kept testing the previous one — exactly the `--shell-file` trap of chapter 118. Now an always-run custom target, copying an **allowlist** of asset subdirectories (`textures`, `projects`), because `assets/` also holds `saves/` and chapter 128 is what a denylist does when it forgets a line. ⚠️ **the mutation harness itself was the thing that lied**: restoring a file's *content* but pushing its *mtime backwards* left the previous mutation's object in the build, so from M4 on every run carried two mutations and the second one crashed the suite — a suspiciously tidy 14/15 that meant nothing. Fixed (`os.utime` + a printed post-restore baseline): **15/15 killed, baseline green**, plus 4/4 on the page itself. ❌ a cover is an **existing texture**, not a title card or a captured frame — capture is a fourth origin into `.hrt` and is deliberately not opened here. ❌ **no `/play/<hash>`**: the link points at a manifest in the working tree, not at a published release; serving a release by id needs the server. ❌ the markdown renderer is a subset (headings, fenced code, pipe tables, lists, `code`, **bold** — no links, images, nested lists or blockquotes). ❌ Chrome only. ❌ nothing re-bakes the index on a manifest edit; the test only catches it afterwards. |
| Golden path publish → promote → audit | ✅ verified interactively (three channels + audit log) |
| Backup/restore drill | ✅ verified (chapter 98, executed) |
| SDL2 build split, both directions | ✅ verified natively, and configure inside the Drogon container correctly skips SDL2 |
| **Docker container builds and answers `/healthz`** | ✅ **verified 2026-09-07 (chapter 142)** — built, started and probed on this machine under amd64 emulation, and a `baas-docker` CI job now does it on every push: the image builds, the container is **still running**, `/healthz` answers, a guest signs in against the seeded project, and the `/openapi.json` it serves is identical to the one committed here. ❌ **before it, and worse than "unproven": the image had never served a request.** `CMD` ended in `--seed`, and `--seed` printed the demo key and `return 0` — so the container seeded and exited, every time, and `restart: unless-stopped` in `docker-compose.yml` turned that into a loop that never listened. Measured, not inferred: `running: false  exit: 0`, nothing on the port. No test could have caught it — all ninety link `baas_core` and call `register_routes()` themselves, so none had ever run `main()`. `--seed` now serves and `--seed-only` is the one-shot, and `test_baas_boot` spawns the real binary and checks both directions from outside. ⚠️ the CI job itself has not run on a GitHub runner yet; ⚠️ `docker compose up` has still never been run (its healthcheck *can* now pass — `curl` is present in the runtime image — but nobody has watched it). |
| **OpenAPI description of `/v1`** | ✅ **verified 2026-09-07 (chapter 142)**, and ❌ before it — there was none. 51 operations generated from one table (`baas/openapi/spec.cc`), served at `GET /openapi.json`, committed at `baas/openapi.json`. Two tests keep it from drifting: the set of (METHOD, path) Drogon actually registered must equal the documented set **both ways**, and re-baking must produce the same bytes. No exemptions — the WebSocket route, which Drogon lists once per HTTP method, is collapsed by a rule that reads Drogon's own handler description, not by a name in a test. ⚠️ the document describes request/response bodies as prose and "an object", not as JSON Schema: a hand-written schema is a second table to drift, and that trade was made deliberately. |
| **PostgreSQL production adapter** | ⚠️ **the code path works, the deployment has never happened.** The whole baas test suite passes against a real Postgres (chapter 141, 30/30) and CI runs it there, so the queries, the schema, the parameters and the locking are proven. What is unproven is everything outside `ctest`: no `baas` process has ever booted against Postgres, the pool size (4) has never been measured, and production is still single-node SQLite. |
| **CI green run** | ⚠️ **unknown from this machine.** `ci.yml` self-documents "written but not run in the authoring environment"; `gh` is unauthenticated here. |
| Purchase affordability check | ✅ verified 2026-09-07 (chapter 140): a transaction plus `db::lock_clause()`, and `test_baas_concurrency` runs eight threads against one purse — exactly ten of forty-eight purchases succeed, the balance ends at zero and never below it. ⚠️ **the test has no teeth on SQLite** and says so in its own header: the pool is one connection, so there is no interleaving for a lock to prevent. What it does prove is the absence of a deadlock, which it earns — the first version of these transactions had one. |
| The other read-then-writes | ✅ verified 2026-09-07 (chapter 140), and ❌ **before it**. The comment that said "atomic because the SQLite pool is size 1" was true of `purchase`, which opens a transaction, and false of `grant` and `consume`, which did not — a pool of one hands out the connection for a STATEMENT, not for a sequence. And the failure was not a lost update: eight threads granting a player's FIRST item both took the INSERT branch and the second died on the unique index, uncaught, **terminating the process**. `inv::grant` is reachable from `POST /v1/inventory/grant`, so that was a two-request remote kill, sitting there since chapter 102. `lb::submit` and the rating pair are now under the same treatment. |
| Postgres | ✅ **works** — verified 2026-09-07 (chapter 141): **30/30 baas tests against a real `postgres:16-alpine`**, and CI now runs the whole suite **twice**, once per backend. ❌ **before it**, and `db.h` had called it "a documented deploy-time build" since slice one. It took four runs of `baas/ops/pg-test.sh` to get there and each found a different class of failure, all four invisible on SQLite and all four in code that had tests: (1) Drogon does not translate `?` into `$1` — now `db::portable`, one pure function with four rewrites (`?`, `AUTOID`, `CURRENT_TIMESTAMP`, `BYTELEN`), behind `db::exec`/`db::insert_id`, with a source scan in `test_baas_sql_seam` that fails if any of the 104 call sites goes around it; (2) Drogon binds integers in BINARY at `sizeof(T)` with no type OID, so a C++ `long` into an INTEGER column is `incorrect binary data format in bind parameter 1` — Postgres parameters go as text, which has no width; (3) `FOR UPDATE` cannot lock a row that does not exist, so chapter 140 had closed the SECOND write to a key and left the FIRST one racing — materialise-then-lock (`db::ensure_row`), and four more services (`cfg::set`, `store::upsert`, `save::put`, `asset::put`) turned out to have the same shape with **no transaction at all**, where the loser is an uncaught exception on an event-loop thread; (4) Drogon ENQUEUES its COMMIT, so with a real pool the next statement takes a different connection and reads from before it — `grant` returned 8 and the GET after it returned 7. `db::Transaction` waits for the commit (bounded at 10 s). ⚠️ **the CI job itself has never run on a GitHub runner**; the Postgres service was exercised locally against the same image. ⚠️ **the server has never been booted against Postgres** — `--seed`, `--static` and a live `baas` process are still SQLite-only in practice; only the test suite has run there. |
| **Architecture decision index** | ✅ added 2026-09-07 (chapter 143) — `docs/adr/README.md`, 63 decisions, each one line pointing at the chapter that argues it, and **13 marked `Superseded by N`** so a reversed decision is visible rather than folklore. No new prose: the chapter is the argument. `test_adr_index` checks the three ways such an index rots (a renamed chapter, a duplicated id, a supersession naming a row that is not there) and caught one on the first run. ⚠️ it checks LINKS, not CONTENT — a row that describes its decision wrongly still passes. |
| **The Studio's divider, and a strip that says two things at once** | ✅ verified 2026-09-07 (chapter 144), closing a note chapter 112 left over the fixed split: *"a draggable one needs a cursor shape, a hit zone and a persisted position, and no second author has asked for it yet."* All three now exist. The rule with a design in it is the third: **the stored width is never clamped and the drawn width always is**, so a window you shrank once cannot take your layout away — `fit_inspector` is a function of today's window, and the test asserts a stored 900 draws 250 in a 500px body and 900 again in a 2400px one. `platform::set_cursor` had existed since the seam was written with **no caller at all**; a Scene now reports a `ui::CursorHint` and `App` applies it, because most scenes compile into tests that link no backend. The status strip became a list of cells with per-cell tone: it was one string coloured `warn` whenever the document was dirty, so the Pixels editor drew the hex code under the cursor **in the colour of a warning**. Scene grid/snap came with it (`sandbox::snap_to`, rounding away from zero on both sides of the origin; the drag snaps AFTER the grab offset so the actor lands on the grid, not the pixel you grabbed). 25/25 mutations killed, two of them real survivors first (an empty cell still drew a separator; the grid button stayed live while the scene played). ⚠️ **no hand has dragged it in a real window** — the cursor change is asserted as a `CursorHint` in a headless test, and `backend_sdl.cpp`'s `set_cursor` runs for the first time ever unwatched. ⚠️ the full-screen labs share the splitter and the file by construction; only the Studio's tab has a test. ⚠️ `studio.layout` on web/IDBFS is untested. |
| **The farm has seasons** | ✅ verified 2026-09-07 (chapter 143), and ❌ **for eighteen chapters before it**: `CropDef::season` was parsed by `defs.cpp` and read by nobody — not at planting, not at the day boundary, not on screen. A field written and never read is a claim, not data. Now four seasons of seven days, a refusal that names the season, withering at the boundary (soil survives), and one `grows_in` behind both moments. Visible three ways — HUD calendar, morning report, dim seed chip — the last of which is a decision expressed as a COLOUR and was the mutation that survived until `count_colour` existed. ⚠️ **no hand-played season turn in a real window**; the seven sleeps are simulated and the screenshot is day 1. ⚠️ the eight crops' economy is estimated, not measured. |
| Scene visual output | ⚠️ mostly manual visual accept. Two structural golden tests now exist (`test_ui_golden`, `test_shell_golden` — the latter now covers six sections, the Project panel's healthy/holed/unreadable states and the audit-log ordering); the rest is eyeball. |
| Studio shell renders correctly | ✅ verified 2026-09-04 by offscreen render at the real 1280×720×2, inspected as an image. **The window itself was not opened** — screen capture is unavailable here, so SDL's `present()` path is untested. |
| Command registry + undo/autosave, **and a Studio that uses them** | ✅ verified 2026-09-04 (chapters 111–112, extended 116): 71 tests. `--cmd` runs real operations, the old flags are aliases onto it, and `Cmd+K` in the Studio lists the same registry. The Map workspace pushes every edit onto `CommandStack`, autosaves on a timer and offers recovery on open — the "no consumer" gap from chapter 111 is closed. As of chapter 116 there are **two** workspaces (Map and Scene) behind a `Workspace` interface, and `--lab scene` (then `--sandbox`) runs the same Scene workspace full-screen. ✅ **extended 2026-09-06 (chapter 133)**: the Scene workspace grew the four effect labs as components, and got the test it had never had — `test_scene_workspace`, whose questions are about *reach* (`control_rect(id)` reports an empty rect for a control scrolled out of the viewport, and the helper that clicks one fails if scrolling never brings it into view). |
| Shared 2D map format (`map2`) + fpsmap1 migration | ✅ verified 2026-09-04 (chapters 110, 112): `--fps` reads the real authored level through the new path, `test_fps` asserts it is identical grid-for-grid to the legacy parser, and the Studio's Map workspace now **renders and edits** a map2. `test_map_workspace` ends by reading a file the editor wrote with the raycaster's own loader. Building that consumer found a real hole: `to_text`/`load` could not round-trip a tiles layer with no tileset — the shape an editor produces before there is art. ✅ **completed 2026-09-06 (chapter 132)**: Map Lab is deleted, `--lab map` is now the Map workspace full-screen, and `fps::to_text`/`from_text` went with it — `map2` is the only format anything writes, and `tilemap::from_fpsmap1` is the one remaining reader for old files (`--cmd map.migrate` converts one). `assets/maps/level_00.map` became `level_00.map2`, and the evidence outlived the file: `test_fps` embeds the exact bytes it held for 120 chapters and asserts they migrate to the committed map2 **byte for byte** — a stronger check than the old one, which compared two readers of the same living file. ⚠️ triggers still have no editing UI. |
| UI layer: keyboard, focus, clipping, modals, text editing | ✅ verified 2026-09-04 (chapter 109): 62 tests including a full text editor and two mutation checks; confirmation screen rendered offscreen with its negative control. ❌ **the window itself has still never been opened**, and resizing has never been performed. |
| TWO games, both playable from one link | ✅ verified 2026-09-07 (chapter 137). `--project projects/creatures.gameproject` is a route with long grass, an ambush, a battle screen driven by thumb, a party that levels and evolves, and a save; inspect/publish/verify/hub work on it unchanged and the collection page lists three cards. What the second game cost the first is the interesting part: the promise recorded in CLAUDE.md — *"if a second game needs the on-screen controls, farm/controls.hpp will have to split"* — came due, and `engine/ui/touch.hpp` took the parts that are facts about a HAND while each game kept its own LAYOUT (one screen laid out for the other's neighbours is the bug that sharing would buy). `farm::Theme` became `tilemap::Theme`. ⚠️ **the farm's browser proof does not transfer** and that was worth finding: holding east there ends at Save, holding east here ends in a fight where the save button does not exist. The creature run now drives the whole loop — into the grass, Run, acknowledge, save — which is a stronger claim, and CI runs both. ❌ ONE route, no trainers, no gym, no PC box, no items, no NPCs, no dialogue, no cloud save; the battle screen does not animate. |
| The simulation computes the same thing on two instruction sets | ✅ verified 2026-09-07 (chapter 138). `assets/creatures/reference.crep` is one recorded battle — start state, a hash after every turn, and a fingerprint of the tables it was played under — written by macOS/arm64/clang. `test_creature` re-bakes it from the tables and compares **bytes**, the standard a `.recipe` is held to, and CI runs that on Ubuntu/x86-64/gcc. `--cmd creature.verify` re-plays it and reports the first disagreeing turn. Before this, every determinism check ran the sim twice inside ONE process, which proves `step` is a pure function and says nothing about two toolchains agreeing about the result. The wasm build runs the same verify through `?cmd=` in `web-build`, so the target this project actually ships on is the third. ⚠️ two architectures and one wasm run is not "every machine": no Windows/MSVC, no 32-bit, no big-endian. ⚠️ ONE reference battle, 15 turns; it visits Move, Switch and Ball but not `Run`, and not a hundred-turn stall. ❌ nothing sends a recording anywhere — the BaaS replay store exists and this game does not touch it (S28b). |
| A recording is complete | ✅ verified 2026-09-07 (chapter 138). Throwing a ball used to be resolved beside `step`, and when it failed the game told the resolver the player had **switched to the slot already active** so the wild side would get its move — a legal no-op chosen for its side effect. The consequence was that no replay of this game could contain the verb the genre is named after. `Action::Kind::Ball` fixed it and the RNG stream did not move (same draws, same order — `test_creature_world`'s balance sample came out identical to the digit). ⚠️ `settle` now checks `caught` before `winner`, because a ball that sticks sets `winner = 0` and the old branch would have read that as a knockout. |
| One battle, two machines | ✅ verified 2026-09-07 (chapter 139). Each side sends the ACTION it chose and both compute the turn: **34 bytes a turn on the wire, 16 of them the hash**. `test_netbattle` plays a thousand matches by handing two objects each other's frames — half of them delivered out of order — and every one ends at the same state with the same tape, which the offline verifier then accepts. Forty more are played with one peer holding deliberately re-tuned tables: **all forty desync, both sides notice, at the same turn**, and none reports anything to the ladder. `test_creature_pvp_live` plays one over a real WebSocket against a real Drogon server through the real SDK, and `--pvp` × 2 does it between two OS processes. ⚠️ **no referee**: the server assigns sides, seeds and pairings and rates only matches it made, but it does not replay the battle, so two clients modified the same way still agree. ❌ no reconnection, no timeout, no surrender. ❌ **the game has no PvP UI** — `--pvp` is headless and plays with the AI. ❌ the hub (rooms, queue, and now the match ledger) is single-node and in memory. |
| A rating, not a score | ✅ verified 2026-09-07 (chapter 139). Three things had to change and each is a rule, not a feature. (1) A leaderboard that keeps your BEST cannot hold a rating — an Elo goes down — so boards gained a `mode` column (migration 9; `'best'` is the default, so every shipped board is untouched). (2) A ladder where the client picks the NUMBER is not a ladder: `POST /v1/leaderboards/{key}/match` takes the outcome and the server does the arithmetic, sharing `engine/elo.hpp` rather than keeping a second copy of the curve. (3) Nor one where it picks the OPPONENT: the hub remembers its last 4096 pairings and the endpoint refuses a match it did not make. Both players report the same match, so it is idempotent on the match id — which graduated the idempotency store out of `inv_service.cc` under a note that had been waiting there since chapter 102 for a second consumer. ⚠️ K is fixed at 32 for everybody; no floors, no placements, no decay. ⚠️ the pairing ledger dies with the process, so a report after a restart is refused. |
| A stalemate ends | ✅ verified 2026-09-07 (chapter 139), and ❌ **before it, for three chapters**. A battle where nobody has PP left cannot deal damage, and `choose`'s last resort was "switch to the first living creature" whatever its PP — so two IDENTICAL teams rotated their benches at each other forever. Every test in the repo built its parties from randomly drawn species, so **no test had ever put two identical teams in a battle**; two real `--pvp` processes did it on the first try and ran 500 turns. Two guards, tested apart: the AI no longer switches to a bench that cannot act (a mirror match now DECIDES, in 28 turns), and a battle reaching `kMaxTurns` (200) is a DRAW — a rule in `battle.hpp`, so the wild game, a stored replay and a rated match all get it at once. 200 is far past any real fight, so `reference.crep` still bakes to the same bytes. |
| A route that can be walked, and won | ✅ verified 2026-09-07 (chapter 137): a level-5 starter playing reasonably wins 75–87% of the near grass, all three starters measured, with real losses. A route where the first encounter usually ends the run is a route nobody gets past, and no other assertion in the repo would have said so. Two facts moved into the MAP rather than the code, which is chapter 134's rule twice more: whether a tile ambushes you is a ground id, and which table it rolls is a `far` MASK layer — so the two bands look identical and the player finds out by walking into one. ⚠️ balance is measured against the AI playing both sides; no human has played it. |
| A simulation that REPLAYS, byte for byte | ✅ verified 2026-09-07 (chapter 136). `creature_core` is the first thing here that has to produce the same result on two machines: a replay is only a fact if replaying reproduces it, a PvP turn is four bytes, and a desync must be *detected* rather than quietly deciding a winner. Three refusals protect it — **no float in the resolution path** (effectiveness is a percent, STAB is `*150/100`), **the RNG is a hashed field** and only `step` advances it, and **turn order is priority → speed → one draw from the battle's own stream**, never "side 0 first" (which works right until the two sides are two computers). `test_creature` plays **1000 random battles** to the end, replays each, and compares the hash **after every turn** — plus asserts the sample's shape (1000 decided, 11278 turns, longest 28), because a thousand battles that ended on turn one would replay perfectly and prove nothing. `engine/rand.hpp` now has a **golden sequence**: nothing pinned the stream before, so nothing would have noticed it changing — and a changed stream voids every stored replay. 32/32 mutations. ⚠️ **proved on ONE machine**: no battle has replayed on the **web build**, which is the platform the no-float rule exists for. ❌ all moves are physical; no stat stages, items, weather or multi-hit; the AI is one rule and a tiebreak. |
| Eighteen species, none of them drawn | ✅ verified 2026-09-07 (chapter 136): `textures/parts_creature.pix` is **twelve tiles and not one is a creature** — three bodies, three faces, three crests, three tails — and every species is two to four stacked plus two colour swaps. **Evolution is the same recipe plus a part**, and that is *checked*: the test parses the three committed `.mix` files of each of the six lines and asserts each stage is the previous plus exactly one part, earlier parts surviving, swap pairs identical. This is the case chapter 135's fourth `.hrt` door was opened for, at a scale where it stops being a demonstration. ⚠️ **the first sheet was invisible** — bodies at row 3 and crests at rows 1–4 meant the body (composited last) buried them, so the water line's stages 2 and 3 came out pixel-identical to stage 1; every file parsed, every test passed, and only the render said so. Fixed with a layout contract written into the file. ❌ no animation, no back sprites, one 16×16 frame each. |
| "Every `.hrt` source re-bakes to its committed bytes" | ✅ verified 2026-09-07 (chapter 136) — and ⚠️ **only partly true before it**. The rule was a whole-tree sweep for `.pack` imports since chapter 131, but for the other three doors it was **one `.recipe`, one `.pix` and one `.mix` named by hand**: honest with three sources in the repo, dishonest the moment one commit added nineteen. `test_commands` now sweeps every `.recipe`/`.pix`/`.mix` in the tree (24 today), re-bakes each and compares, and **refuses to pass on an empty list**. Verified both ways: one flipped byte in `creature_05.hrt` turns it red and names the file. Third time this project has found this shape (chapter 128's preload denylist, chapter 131's attribution rule): the rule was right, the check covered one instance, and the gap was invisible until the count grew. |
| A second game reaching the platform through a manifest | ✅ verified 2026-09-04 (chapter 113): `projects/farm.gameproject` added a game with a manifest and a scene and **nothing else changed** — `--project`, `--project-inspect`, `--project-package` and `--hub` all work on it unmodified. The farm consumes `map2` layers + entities, `Camera2D`, `save_core` and the Studio's Map workspace. Its first use of `Camera2D` found a real bug: `set_viewport` did not re-clamp, so a world smaller than the window was not centred on the first frame (and a resize would push the view off the world). ❌ **never played in a window** — only driven by synthesized input, so step timing and day length are unmeasured *feel* questions. ❌ **no art**: flat colours and circles. |
| One project resolve, shared by every verb | ✅ verified 2026-09-04 (chapter 114): `engine::inspect()` replaced **four** hand-written copies of read+validate+hash (CLI launch, CLI inspect, publish, hub) and a fifth partial one in the Studio. They had already drifted: publish returned at the **first** missing asset while the other three listed all, so `--project-publish` reported one broken path per run. Verified at the CLI, not only in a unit test — a probe manifest with three broken paths now yields the same three lines from publish and inspect. Four mutation checks (first-problem-only, dropped missing assets, hashing an incomplete project, reordered problems) each break `test_inspect`. |
| Studio Project section (asset browser + validation) | ✅ verified 2026-09-04 (chapter 114): draws the same `engine::inspect` answer `--project-inspect` prints — type, path, content hash, size, present/missing per declared asset, plus the verdict and the package hash this source would publish as. Building it found a real divergence: the scene held its own `known_entries = {"fps"}` while `main.cpp` knew `{"fps","farm"}`, so `--shell projects/farm.gameproject` called the farm project broken while the CLI called it shippable. The list is injected now, with a negative control proving the list is what makes the difference. ❌ **never clicked** — offscreen renders only. ❌ never held a long list (5 assets, no scrolling exercised at scale); nothing watches the filesystem, so `R`/Re-inspect is manual. |
| Operational evidence: the audit log, in a window | ✅ verified 2026-09-04 (chapter 114): `engine::log()` has returned the append-only history as data since the release store existed and **no window had ever drawn it**. Both hub surfaces now do, through the one shared panel. Newest-first, UTC, with the operator's reason as the widest column — pinned by a row-difference test that inverts if the loop is reversed and fires if the block is deleted. Rendering it immediately exposed a real `publish` entry with a **blank reason**, written before D17 existed. ⚠️ no filter and no paging: `engine::log(channel)` supports the first, the panel does not ask for it. |
| Play viewport: a game running inside the Studio | ✅ verified 2026-09-04 (chapter 115): the scene gets its **own** framebuffer at the game's native size — the alternative (drawing into the Studio's under a clip) would make every coordinate a lie, since a scene asks the renderer how big the screen is. Pause and Step-one-frame are why it beats launching the game. Proven twice: a probe scene that counts its own ticks (six mutations break it), then a real `farm::FarmScene` running 180 fixed steps inside a real `StudioShellScene`, with the nav rail asserted untouched. Writing the test found a real bug: "receiving no input" was a default `InputState`, whose mouse sits at (0,0) — a real position, so an unfocused game was told the pointer was parked in its top-left corner. ❌ **still never clicked**. ❌ the mouse does not reach the game at all (deliberate: a half-correct pointer is worse than none). ❌ only `farm` has been played; `fps` is in the table and untested there. ⚠️ the viewport keeps running on other sections — deliberate, but an expensive scene costs frame time in the Map workspace with no warning. |
| One entry table (launch · validate · play) | ✅ verified 2026-09-04 (chapter 115): `launch_entry` was a chain of `if`s with a `kKnownEntries` literal beside it and a comment asking a human to keep them in sync. The Play viewport would have been the third reader, so the three collapsed into one `entries()` table with `known_entries()` derived from it. A game can no longer be launchable-but-unknown or known-but-unlaunchable. |
| Fixed-timestep clock, shared not copied | ✅ verified 2026-09-04 (chapter 115): extracted from `App::frame` into header-only pure `engine::FixedStep` so the Play viewport runs on the same clamp rather than a second one that agrees on ordinary frames and diverges when the machine stalls. Four mutations break `test_fixed_step`, including the exact truncation bug the farm clock had in chapter 113. |
| A second workspace, and the interface it earned | ✅ verified 2026-09-04 (chapter 116): chapter 112 deliberately shipped one workspace and NO interface ("a shape with one occupant"). The second one is the absorbed sandbox, and having two changed the shape before the second existed: `status()`/`hint()` moved onto the workspace (a scene has no tiles, so the shell had to stop knowing what document it held), `inspector_width()` became a request the shell overrules, and recovery became defaulted rather than pure. Eight mutations break the suite. ⚠️ still no pan/zoom, multi-select or copy/paste in the scene canvas; Spawner/OnOverlap round-trip but have no inspector. |
| `--lab scene` (was `--sandbox`) absorbed, not reimplemented | ✅ verified 2026-09-04 (chapter 116): `sandbox_scene.{hpp,cpp}` deleted (315 lines); it is a `WorkspaceHost` around the same object the Studio's Scene tab holds. What the sandbox gained without anything being written for it: undo (it had none), autosave + recovery, the command palette, the post-chapter-109 widgets, input handling in `update()` instead of `render()`. The host path is covered headless, including that `scene.play`/`scene.undo` are registered there. ✅ and it is the mechanism that finally retired Map Lab in chapter 132: `--lab map` is a `WorkspaceHost` around the Studio's Map tab, and the thing that had kept the old lab alive — being the only place a spawn could be authored — was closed by the Entity tool. |
| Scene editing has undo | ✅ verified 2026-09-04 (chapter 116): whole-scene snapshot commands over the existing `to_scene`/`from_scene`. A drag is ONE undo step, asserted behaviourally (one undo returns the actor all the way, not partway). Writing the test found a real bug: `push_apply` re-installs the world, which rebuilds every entity and cleared the selection — so every edit deselected the actor being edited. ⚠️ O(n) scene text per edit; fine at tens of actors, and the fix at thousands is per-edit commands. |
| A game that DEPENDS on the backend | ✅ verified 2026-09-04 (chapter 117): the farm takes prices from remote config, lets a live event override them again, reconciles its save with the cloud copy and emits `day_end`/`sale`/`harvest`. Colony demonstrated the BaaS; the farm is the first thing that breaks if the backend is wrong. Proven **against a real Drogon server** (`test_farm_live`): an operator changes a price through `PUT /v1/admin/config/farm_defs` and a running `FarmScene` charges the new one; the festival switched on through `POST /v1/admin/events` lands on top of it. ❌ `test_farm_live` **does not run in CI** — CI installs SDL2 only, so every Drogon-guarded test is absent there. ❌ remote config is fetched once at startup: a mid-session price change needs a restart. ❌ analytics are fire-and-forget, so an offline day is missing from the numbers. |
| Cloud save that cannot destroy work | ✅ verified 2026-09-04 (chapter 117): `farm::decide_sync` is a pure, total table over (local hash, local bookmark, remote version+hash). Content is compared before versions; a 404 is an empty slot but a 500 is not, and "we do not know" never becomes an upload; an unreadable cloud save is left alone; both-sides-moved asks the player (F6/F7) rather than choosing. Six mutations break `test_farm`, seven more break the scene test. ⚠️ resolution is all-or-nothing — no merge, which is right for a farm and would not be for a shared world. |
| Guest identity that survives a launch | ✅ verified 2026-09-04 (chapter 117), and found by the end-to-end test rather than by reading: `auth().guest()` created a **new user every launch**, so a cloud save could be uploaded and never read back — upload worked, download worked, the feature did not. Migrations 7+8 add a nullable `device_id` with a unique index; the client keeps one opaque id per installation. Colony has had this bug since chapter 57 and never noticed because it never reads a save back. ❌ per **installation**, not per player: two people on one machine share a farm, one person on two machines has two, until they register a real account — and there is no UI for that. |
| One number in one place (`sell`) | ✅ verified 2026-09-04 (chapter 117): `crops.def` and `items.def` both carried a crop's price, and `end_day` read the item's copy — so a crop's `sell` was the one number a balance pass could not change, from a file or from a dashboard. They agreed, so nothing looked wrong. The rule is now explicit in code and the duplicate data is deleted. Fourth costume of the same failure (ch. 114 four project resolves, ch. 115 two entry lists, ch. 116 two scene editors). |
| Startup that cannot race itself | ✅ verified 2026-09-04 (chapter 117): remote config and live events are chained, not fired together — the fake transport drains **in reverse** so a parallel version fails immediately. And a save pressed while the first sync is in flight no longer uploads: found live (a save pushed as v1 came back as v2), pinned in the unit test by holding the GET open on purpose. The harmful mirror image is a `Pull` decided from a stale snapshot overwriting the save just made. |
| **The engine running in a browser** | ✅ verified 2026-09-04 (chapter 118) — **for the first time**. Every earlier "web build green" meant the Emscripten link succeeded; the page had never been opened, and when it was, it did not run (`SDL_CreateRenderer failed: Couldn't find matching render driver`). Chess, the farm and the Studio shell now all render in Chrome, served by the BaaS itself (`baas --static build-web`) so the page and the API share one origin. The farm reaches the backend from WASM: guest sign-in, remote config, live events, and the chapter-117 sync (`cloud empty` → `cloud v1` after a save). Keys reach the game (F5 dispatched over CDP saved and uploaded). ❌ **software renderer only** — the harness runs `--disable-gpu`, so WebGL and the linear supersample downsample are untested. ❌ **nothing was clicked**: keys were dispatched, the mouse was not. ❌ one browser (Chrome 152, headless, macOS): no Firefox, Safari, mobile or touch. |
| Web build shipped no local state | ✅ fixed and verified 2026-09-04 (chapter 118). `--preload-file assets@assets` was packing `saves/`, `releases/` and `channels/` — gitignored precisely because they are the developer's machine. The published bundle therefore contained `saves/device.id`, so **every browser signed in as the same guest**: two strangers would have shared one farm, one save, one inventory. It also shipped the save files, the channel pointers and `releases/audit.log` (operator actions and their reasons). Found by measuring, not reading: two IndexedDB-wiped "fresh installations" produced byte-identical device ids. After three `--exclude-file` patterns the same test gives two different ids and two accounts; the preload manifest went 47 → 36 entries. ⚠️ the exclude list is maintained by hand — a fourth gitignored directory under `assets/` would ship. The new CI grep is the backstop and knows only those three names. |
| Web saves survive a reload | ✅ verified 2026-09-04 (chapter 118): `saves/` is mounted on IDBFS by the page before `main()` runs (`addRunDependency`), and `assets::` flushes it — serialised and coalesced, because overlapping `FS.syncfs` calls interleave and brought `slot1.sav` back **zero bytes**. Proven with the understudy removed: with every `/v1/` request blocked, the farm still resumes on the saved day and the chip reads `offline`. Before this the web build had no memory at all, and only looked like it did because the cloud copy was quietly standing in. ❌ private-mode / blocked-storage path is handled (the page says so) but not exercised. |
| **Someone clicked it** | ✅ verified 2026-09-04 (chapter 119) — the ledger's longest-standing ❌. Real mouse clicks in a browser drove the Studio: the Project section opened its asset browser, Play → Play ran the FPS raycaster inside the viewport, and a click on the map canvas **painted a tile** (tab → `Map *`, status `tile 7, 7`, Undo enabled, hint `undo: paint`). Everything the offscreen tests claimed held. Worth recording: the first two clicks appeared to do nothing, and that was a bug in the TEST — coordinates read off a screenshot of a CSS-scaled canvas (0.82×), landing 8 px outside the nav rail. "The click did nothing" is indistinguishable from a real input bug; the cheap discriminator is asking the DOM and the app what each saw. ❌ no wheel, no right-click, no drag driven in a browser; ❌ no touch at all. |
| Mouse into the Play viewport | ✅ verified 2026-09-04 (chapter 119). Chapter 115 deliberately gave the embedded game **no** pointer, because the transform could not be checked — the shell had never been clicked. It can now, so the arithmetic is done: mapped through the rect `draw()` blitted into (not a stored scale — the panel can be smaller than one native frame, and then the blit is fitted), one frame behind on purpose (the alternative is a second copy of the layout), and a press that starts inside keeps the pointer until release so a drag off the picture cannot leave the game holding a button. Proven end to end: a click on a tile inside the farm **running in the Studio's Play viewport in a browser** tilled that tile — six transforms deep. Five mutations break the suite. |
| A game that consumes a pointer | ✅ verified 2026-09-04 (chapter 119): the farm is the first thing in this project to read a mouse position — hover an **adjacent** tile to face it, click to use the tool on it, the same reach the keyboard had. A transform nothing reads is a number, not a feature (D15). ⚠️ one mutation SURVIVES and is recorded rather than hidden: removing the `mouse_x >= 0` guard does not fail the suite, because with the whole farm on screen the camera centres it and screen (-1,-1) maps outside the player's neighbours anyway. The guard stays (it is the platform contract, and reading -1 as a position is the chapter-115 bug) but the assertion beside it is a contract check, not proof. ❌ the raycaster and the colony still ignore the mouse, so the viewport transform has exactly one witness. |
| CLI surface: one door per kind | ✅ verified 2026-09-04 (chapter 120): 31 flags → three kinds. A **game** is launched from its manifest, the **Studio** is `--shell`, and every other windowed thing is a **lab** behind `--lab [id]`, derived from one table the way `entries()` already was. `--hub-ui` deleted outright along with `hub_scene.{hpp,cpp}` (the Studio's Hub section draws the same panel from the same view model); twelve flags folded, nothing removed that was the only door to a room — Map Lab is still the only entity/spawn editor, and seven labs are the only runtime consumers of `particles_core`, `light_core`, `audio_core`, `tween_core`, `render3d_core`, `viz3d_core`, `studio_core`. **Chapter 133 closed four of those seven**: the fx/light/audio/anim labs are gone and their cores are consumed by scene components instead — absorb first, delete second, because the audio lab was `audio::Mixer`'s only caller anywhere. An unknown flag used to fall through to the M0 demo **silently**; it is now an error carrying the full list plus a retired-flag map, and `demo --help` prints the same. ❌ **no test covers the CLI surface** — CI's smoke script covers the spine verbs only; `--lab` and `--help` were checked by running them. |
| Reading a format we did not invent | ✅ verified 2026-09-05 (chapter 121): hand-written DEFLATE (`inflate_core`, RFC 1951+1950 — stored/fixed/dynamic blocks, LZ77, verified Adler-32) and a PNG decoder on top of it (`png_core`: depth 8, colour types 0/2/3/6, tRNS, all five filters, non-interlaced; everything else refused **by name**). Decode only — nothing writes a PNG, and a compressor has no consumer here. Tested against streams a **real zlib** produced at levels chosen so all three block types appear, plus four corruption modes that must be refused rather than decoded into rubbish. Three PNG fixtures are hand-built so their pixels are known by construction (one row per filter, a tRNS shorter than its palette, greyscale); the fourth and fifth are files this project did not make. ❌ no encoder, no interlace, no 16-bit. |
| Foreign art has one door | ✅ verified 2026-09-05 (chapter 121, extended in 122): `--cmd asset.import <src.png> <dst.hrt>` is offline — the engine never decodes PNG at runtime — so an open-licence pack and art drawn in the Texture Lab arrive downstream as the same format, and the asset cache, resource closure and package hash have one kind of file to think about. `assets/ATTRIBUTION.md` records source, author, licence and the command that reproduces the import; the source PNG is committed next to the `.hrt` so the import can be re-run rather than taken on trust. Four `register_release_commands` call sites became one `cmd::register_all`. ⚠️ the 5 KB import source ships in the web preload, deliberately. |
| The farm has art | ✅ verified 2026-09-05 (chapter 121): Kenney Tiny Town (CC0, 16 px, 12 × 11). `tilemap::Tileset` cuts the sheet once at load; the map keeps **semantic** ids and `assets/farm/theme.def` joins id → sheet index, so swapping art is a new sheet and at most a new theme file — no rebuild, and no renumbering the level. An id with **no line has no art** and falls back to the flat colour, which is what makes "support both" per-tile: Tiny Town has no water tile, so the pond stays flat while everything around it is themed, and the test states that in pixels. Verified in a browser, with a mouse click tilling a themed tile. ⚠️ two mutations survived at first because `draw_tile`'s guards masked each other — fixed by having one condition, not three. ❌ no autotiling (grass meets dirt with a hard edge), one sheet per map, and `map2`'s own tileset field is still unused. |
| Art this project drew, in the same pipeline | ✅ verified 2026-09-05 (chapter 122): the second half of the "support both" decision. `--cmd asset.texture <src.recipe> <dst.hrt>` bakes a Texture Lab recipe to `.hrt` offline, so a tile from a CC0 pack and a tile the project made are the same kind of file downstream. The theme now names its sheets and may declare several, which is what keeps our art OUT of the imported file — compositing into `town.hrt` would make the derived image impossible to attribute and would be erased by the next re-import. The farm's pond is the first tile through the door: 16×16, two shades of blue, `assets/textures/farm_water.{recipe,hrt}`. `test_farm` regenerates it from the committed recipe and compares **bytes**, so "drawn in the Studio" is checked, not claimed — and that also pins the generator's long-standing purity claim. ⚠️ the recipe parser is deliberately tolerant (unknown keys keep defaults), which behind a *writing* command would have baked the DEFAULT texture over the destination and reported success; it now reports how many keys it recognised and the command refuses zero. ⚠️ one mutation survived: the sheet range check had never been reached, because no file in the repo has a wrong index — fixed by a test on a **copied** asset tree, not by deleting the guard. ❌ still no autotiling (the pond is a hard-edged rectangle), one tile repeated with no variants, the water does not move although `make_sheet` could animate it for free, and the Texture Lab can generate a texture but cannot draw a SHAPE — so "clone and redraw" is proven for textures only. |
| Somewhere to draw | ✅ verified 2026-09-05 (chapter 123): a third `studioshell::Workspace` — a pixel editor over `.hrt`, with pencil/rect/fill/eyedropper, undo through the same `doc::CommandStack` the map and scene use, autosave and recovery on a BINARY document, and a palette **sampled from the open image** rather than a fixed ramp. It takes a LIST of the project's textures because the farm's first texture is the imported Kenney sheet, which is the one file our own pixels must never enter. `pixel.next` and the inspector list are one operation (`open_index`), which REFUSES to switch while dirty — there is no undo across a reload. Tested headless like the map workspace, and the claim it pins is the ROUND TRIP: paint, undo across two different previous colours, save, then read the file back and DECODE it. Verified in a browser running WASM. ⚠️ it exists but nothing has been drawn with it yet — the narrow path pieces that motivated it are still not in the sheet. ✅ **it can now reach a colour the sheet does not have** (chapter 127): both of its ways of choosing one read the FILE — the palette is the image's own most-used colours and the eyedropper is a pixel — so the reachable set was exactly the set already there, which makes a retouching tool rather than a drawing one. Two doors, because they answer different questions: three **HSV** sliders for *"a bit darker, same colour"* (a shade is one axis in HSV and three correlated ones in RGB) and a **hex field** for *"#8B5A2B, the one the pack uses"* (a slider is a pixel-per-step drag and cannot be told an exact triple). The arithmetic is a pure core (`engine/paint/colour.hpp`) whose round trip is **exact, not close** — verified over the whole cube, 16,777,216 colours, 0 mismatches; CI runs a 65,536-colour slice because the full sweep costs 76 s. The mixer holds `paint::Hsv`, NOT a colour: every colour with v=0 is black, so deriving the sliders each frame would make a drag to the bottom forget the hue and return WHITE on the way up — a one-way slider. Every way of choosing a colour goes through one `adopt()`, so the field, the sliders and the brush cannot disagree. ⚠️ the guard that stands the letter shortcuts down while the code field has focus (B, R, G, I are hex digits) **shipped a worse bug than it fixed**: `ui::Context` only moved focus when another WIDGET took it, so a press on the canvas left the field holding the keyboard and every letter shortcut stayed dead for the session — fixed by one line in `ui::Context::end()`, tested in BOTH directions. ⚠️ adding ~130 px to the inspector found that `ui::slot()` **clamps**, so a control that does not fit gets a rect of height 0: drawn as nothing, hit as nothing, reported by nothing — the chapter-126 bug from the other side. The workspace now asks for the height, compares what came back, and says so on the status line (outside the panel, because a panel too short has no room to report it). ⚠️ a rendered frame — not any of the 76 tests — found the `COLOUR` label running off the 280 px panel as "…RMB era". ❌ **still cannot create a file** (it edits only the textures the manifest declares — the last ceiling between "there is an editor" and "new art is drawn in the Studio"), no 2D saturation/value square, mixed colours are not added to the palette (the eyedropper is the recall), the inspector does not scroll, one layer, no selection/move/copy, no canvas resize, tile guide fixed at 16 px. |
| Keyboard input on the web | ✅ verified 2026-09-05 (chapter 123) — and ❌ **before** it, silently, since chapter 118. SDL2's Emscripten backend listens for keys on the CANVAS element; the page never focused it, so `document.activeElement` was BODY and no keydown ever reached SDL. Every web build shipped with NO keyboard: no WASD in the farm, no F5/F9, no chess input, no Studio shortcuts. It survived three chapters of browser verification because every check used the **mouse**, which SDL registers on the canvas the page passes in `Module.canvas`. Fixed with `tabindex="0"` plus `focus()` on runtime-init and on mousedown. Now verified by holding a key: `R` switches the pixel editor's tool, `D` walks the farm's player east. Behind it sat a second bug it had been hiding: the command modifier was a compile-time `#ifdef __APPLE__` in FOUR files, so the wasm — compiled once, run on every OS — made a Mac user in a browser press Ctrl+S. `InputState::accel()` is now one runtime definition accepting either modifier; measured in the browser, a stroke changes 220 screen pixels and Cmd+Z returns it to 28 (the moved hover cursor). ⚠️ key edges are poll-derived, so a synthesized press+release inside one 16 ms frame is invisible — a driver must HOLD the key, and the first three attempts at this test failed in a way indistinguishable from the bug. |
| Playable by hand | ✅ verified 2026-09-05 (chapter 124): a d-pad and two action buttons over the farm, driven by a REAL touch in a 390×844 phone viewport — holding `>` walks the player east. It reads the POINTER, not touch events, because SDL synthesizes a mouse from a finger: one implementation for tap/click/trackpad, no new event type at the platform seam, and every existing mouse-driven surface (the whole Studio) keeps working on a touch device. ONE `layout()` answers the renderer and the hit test, so a control cannot be drawn in one place and hit in another — the one bug a screenshot cannot show. `consumed` is set by POSITION, not by the button being down, so a tap on the pad does not also till the tile beneath it. Fitting is decided by PROPORTION (at most half the width, two fifths of the height), not pixel thresholds — the first version drew the pad over the 480×270 retro framebuffer. ⚠️ **one finger at a time**: no holding a direction while tapping an action. ⚠️ always drawn, never auto-hidden — there is no reliable way to ask whether a device has touch. ✅ **every verb now has one** (chapter 126): the four-slot hotbar — a PICTURE of the selected tool since chapter 113 — became the control for it, and its geometry moved into `controls.hpp` rather than being copied, because a strip that answers a tap and is drawn from its own constants is two layouts for one screen. `save` sits a row above the thumb row; during a cloud conflict that seat is REPLACED by the two answers (`F6`/`F7`), never shared with `save` — `save_game()` pushes, so saving mid-conflict silently means "mine wins", which must not be offered as a 44px square to somebody who never saw the question. Each button is labelled with the key it duplicates, so the chip that reads `F6 keep yours / F7 take cloud` is already their legend. **Writing it found a freeze**: `update()`'s dialogue branch RETURNS before reading the pointer, so talking to Anna was a permanently frozen game whose only exit was force-quit — live since chapter 124 promised hand-playability, and invisible because `test_farm_scene` had **no dialogue test at all**. The panel is laid out in `controls.hpp` too: tap an option to pick it, tap the panel to advance, no new button anywhere. Geometry is checked as a PROPERTY over ~12k screen sizes (every live box on screen, no two overlapping), because thirteen rectangles cannot be eyeballed. 20 mutations: 18 died at once, and the **two survivors were the same mistake** — `if (b.empty()) return;` deleted from the button renderer, and the pad drawn while the dialogue owns the input. Every test asked *can this be pressed*; nothing asked whether something DRAWN can be pressed at all. Two behavioural claims closed both (a held pointer changes no pixel while the box is up; an absent control leaves no glyph in the corner), then 20/20. ⚠️ a **fourth** bug came only from looking at a rendered frame: the d-pad's `v` button drew through the config-problem chip once the hotbar grew to 44 — now pinned by a test that finds the chip's opaque background and forbids any control across it. ❌ no `F9` button (loading discards the day and there is no modal to confirm with). ❌ still only the farm has any of this; `iso` and `colony` have none. |
| Translucent outlines | ✅ fixed 2026-09-05 (chapter 124) — ❌ **broken and invisible since chapter 69**. `draw_round_rect` painted its four straight edges with an opaque copy and its four corner arcs with the alpha-respecting sink, so ONE call produced solid edges joined by faint curves; `draw_rect` was silently opaque too, and neither has a `_blend` sibling a caller could pick instead. It survived because every outline in the project was opaque until a d-pad wanted a faint one. Opaque still takes the fast path, and `test_aa` now pins both directions plus `fill_round_rect` (which was already correct). |
| Art this project DREW, and the rule that consumes it | ✅ verified 2026-09-06 (chapter 125): the third and last door into `.hrt`. `--cmd asset.pixels <src.pix> <dst.hrt>` bakes an ASCII sheet a person typed, so the three origins — imported, generated, drawn — are one format downstream. Text rather than the Pixel workspace for a specific reason: a 16-piece autotile set is **not sixteen drawings**, it is one corridor profile cut sixteen ways, and what has to be right is the RELATIONSHIP between pieces — a thing you diff and review, not one you verify by clicking through sixteen canvases. `assets/textures/farm_path.{pix,hrt}` is the first through it: 64×64, the sixteen pieces Tiny Town does not have (it ships a nine-piece **patch**, for filling an area; a one-wide path is a **line**). The farm's path autotiles from one theme line, `autotile ground 2 path 0` — the map still stores id 2 everywhere and the corner is a consequence of the map. **`autotile_index` (47-blob) is still unused, and now explained**: down a one-wide path no diagonal ever has both cardinals filled, so only 16 of 47 are reachable across a sheet with 31 undrawable slots — hence `autotile_line_index`, whose index IS the 4-bit mask so the sheet's grid position is the piece's meaning. Checked: `test_commands` re-bakes the `.pix` and compares bytes; `test_farm` pins the **seams** (a piece connecting north presents the shared edge, one connecting nowhere presents nothing) and all sixteen neighbourhoods against a real map. ⚠️ two mutations survived at first — the scene test compares whole frames, which proves the piece *varies* per cell and not that it is the *right* one; fixed by moving the chooser out of the Scene into `farm_core` as the pure `farm::line_piece`, per the spine's own rule. ⚠️ a fourth redundant guard found and deleted (121, 122, 123, 125). ❌ the `.pix` and the `.hrt` can drift (same as `.recipe` — a test catches it, nothing prevents it); the path is the only autotiled material; the Pixel workspace still cannot create a file; frame cost not measured. ✅ **moved 2026-09-07 (chapter 134)**: the `autotile` record left the theme for the MAP — `rule 2 line` under the layer — so "is this material a road or a region" is readable by anything holding a `tilemap::Map`, the Map workspace included. `farm::line_piece` is deleted and `tilemap::rule_piece` is the one neighbour scan for both rules. **`autotile_index` (47-blob) is now reachable**: the Map workspace can set `blob` on a material and previews it — ❌ but no 47-piece artwork exists anywhere in the repo, so nothing renders a region set yet. |
| **The release id names the release** | ✅ fixed 2026-09-07 (chapter 145), and ❌ **wrong since chapter 93** — invisibly, because until this slice every project shipped at least one asset. `package_hash` fingerprinted the RESOURCES and not `project`/`schema`/`entry`, which `build_package` writes into the same file three lines above the hash. Two asset-less games therefore both hashed to `cbf29ce484222325` — the FNV offset basis, the hash of nothing — and publishing the second was refused with "already stored with different bytes". The general case is quieter and worse: two projects sharing their art and differing only in which scene they launch were **one release**. Now the id is the hash of the canonical package body, written once and used by both the hash and the file, so stripping the last line of a `package.txt` and hashing what is left gives the id back (a test). ⚠️ every release id moved once; `assets/releases/` and `assets/channels/` are gitignored machine state, so the migration was "re-publish and re-bake `collection.json`". ⚠️ `test_release_ops` had **asserted the literal `cbf29ce484222325`** with a comment explaining it — the test had encoded the bug as its expected value, which is not a weak test but a test of the wrong invariant. |
| **A colour is a place, and the one you mixed has a home** | ✅ verified 2026-09-07 (chapter 147), closing two lines that had sat under *deliberately deferred*. Saturation and value were two sliders because "`ui::hit` reports a click, not a drag" — a blocker that had already dissolved: `slider` never used `hit`, it has its own press-hold-release loop, and chapter 144's `splitter` copied it. `drag_in` is that loop written once, with `xy_pad` as its third caller. The pad reports `y` DOWNWARD like the framebuffer, so the one flip a colour picker needs lives in the caller where it can be seen, and the arrow keys reach it. A mixed colour now goes into the palette with **Keep** — not a second palette, the same one, which is rebuilt from the IMAGE on open, so a mixed colour survives a reload exactly when you actually used it. Both fit in the 64 px the two sliders freed, because this panel announces rather than scrolls and a control that pushed Save off the bottom would be a feature that broke a verb. ⚠️ **nobody has picked a colour with a hand here** — every drag is a synthetic pointer, and whether 64 px of vertical range is enough to land on a shade you meant is a question a synthetic drag cannot ask. ⚠️ the gradient is one `fill_rect` per pixel (~11,500 a frame) and is in no recorded bench number, because `--bench-ui` does not open the Pixels tab. ⚠️ **the palette still has no order and no removal**: a kept colour goes on the end, and the only way to drop one is to reopen the file. |
| **A rated match a hand can play** | ✅ verified 2026-09-07 (chapter 146), and ❌ **before it, since chapter 139**: rated PvP worked — matchmaking, a battle over a socket, a stored replay, a moved Elo ladder — and **nobody could play one**. `--pvp` is a headless worker whose client picks actions with `choose`; both players in `test_creature_pvp_live` are that AI. There was no path from the game's screen to any of it: not a control drawn where it cannot be pressed, but a **feature with no control at all**. `pvp.hpp`'s own comment had predicted the fix exactly ("a player-driven one would take the action from a screen instead; everything else here is unchanged") and it was right — `set_auto_play(false)` plus one `act()`. Now: an `O` button starts a search, `Mode::Online` is a status line and Cancel (which leaves the SERVER's queue, or the next player is matched with a ghost), the d-pad is gone while queued, and the battle screen is shared through ONE `shown_battle()`/`my_side()`. `test_creatures_online_live` drives a real `CreaturesScene` against a real Drogon server with `--pvp`'s own client as the opponent, and the recording has exactly as many turns as the player tapped moves for. Three bugs found by three different instruments: the party was hand-built and forgot `Party::count` so the wire carried nothing (**the peer's own validation caught it**); `shown_battle()` keyed on the session still being `Playing`, so the RESULT screen — reached after the match by definition — drew the last wild battle; and a stray `break` left the Continue button **hittable and invisible**, which no test could see because they all tap the rect the layout reports. ⚠️ **nobody has played one in a native window or a browser** — every claim is from a headless driver, and the web build's ws transport is a different implementation. ⚠️ the default backend is `127.0.0.1:8080`: no server list, no address field. ⚠️ a mid-match disconnect is a `fail()` and nothing more — no reconnect, no forfeit, no ladder consequence. |
| **A second autotile material, and water that moves** | ✅ verified 2026-09-08 (chapter 148): Creatures' long grass is the first real consumer of the 47-piece `blob` rule, authored as `creature_grass.pix` in canonical index order and baked through the existing `asset.pixels` door. Farm and Creatures use one `AnimatedTileset`: ordinary atlases preserve their old indices; vertically stacked square frames self-describe through `anim::frames_in_sheet`. `farm_water.recipe` adds `frames=4`, and `asset.texture` reaches the existing `make_sheet` operation rather than implementing a second bake. Both sources re-bake byte-identically; two scene tests prove the frame advances; **19/19 mutations killed** after an animated out-of-range survivor exposed a missing test across frame boundaries. ⚠️ timing is a shared 4 fps default, not material metadata. ⚠️ no animated autotile set exists; animation and blob selection are proven on separate sheets. |
| **Five games on the collection page** | ✅ verified 2026-09-07 (chapter 145) — `iso` and `colony` were in the LAB table since chapter 120, so neither had ever been through inspect, package, publish or the hub, and neither appeared on the page you send someone. Two manifests and two lines moved to `entries()`. Driven by Chrome: five games listed, **every cover decoded**, five cards playable, and tapping Play landed in a running colony. `--lab iso` now says where it went rather than "unknown lab". Their covers came through `asset.pixels` (the iso one laid out by the game's own isometric projection, not drawn freehand); a fifth `.hrt` door that bakes a frame of a running game was considered and **refused as out of scope** — that is a decision with its own chapter, not a side effect of needing two thumbnails. ⚠️ neither has been played from its manifest in a native window. ⚠️ the iso sim saves to `farm_save.txt` at the asset root, not under `saves/` and not namespaced by project. ⚠️ `colony` still generates `colony_agent.hrt` at runtime — a gitignored `.hrt` made by C++ rather than by any of the four doors, invisible to the provenance ledger. |
| Game frame cost | ✅ measured for the first time 2026-09-07 (chapter 145). Release, RENDER only, 200 frames: **fps 0.94 ms** (640×400 ss=1) · **farm 2.20 ms** · **creatures 3.27 ms** (both 640×360 ss=2, i.e. 1280×720 physical). Both games draw a full on-screen control pad every frame and both are comfortable inside the 8 ms budget. Before this, `--bench-ui` could only measure the Studio — the gap PROJECT-BRIEF had flagged since chapter 117. ⚠️ **render only**: no `update()`, so the farm's day loop and the colony's job queue are not in these numbers. ⚠️ p95 is not dependable on this laptop. |
| Studio frame cost | ⚠️ **the recorded number was wrong, corrected 2026-09-04 (chapter 117).** Every earlier entry reported "Release ss=2 1.1–1.6 ms" — that was the **ss=1** column. Re-measured, and cross-checked by building the previous commit (`eaa54b4`) in Release and running the same bench: **ss=1 1.0–1.8 ms · ss=2 6–10 ms**, with ss=2 **over the 8 ms budget in 2 of 3 runs**. There is no regression from this slice; the figure had been mislabelled since it was first taken. So the honest statement is: at 1280×720 the Studio is comfortable, and **with supersampling on it is at or past budget** — fill-bound, as the ratio always said (ss=2 ≈ 6× ss=1 at 4× the pixels). ⚠️ `--bench-ui` measures the Studio with **no game running**; a Play viewport rendering a 640×360 scene every frame is not in that number. ⚠️ p95 swings from 2 ms to 90 ms run to run on this laptop, so only medians and ratios are dependable. |

---

## 9. What is planned

### Remaining in Horizon 2 (exit gate has 8 clauses; see `docs/strategy/04`)

- **Segments and experiments** — versioned segments, scheduled configuration/events,
  experiment assignment, exposure events, guardrails, stop/rollback. *This is the largest
  untouched area* and gates clause 6 (exposure/outcome data quality).
- **Production deployment on PostgreSQL** while retaining SQLite local mode; the adapter
  and CI matrix are complete, but no deployed process has used them.
- **Encrypted backups, retention, privacy export/delete, incident runbooks.**
- **SLO dashboards, alerts**, logs dimensioned by release and environment.
- **Remaining failure drills** — bad migration, BaaS unavailable, telemetry unavailable,
  object loss, expired credential, bad config.
- **Engine** — `RenderDevice` ADR + minimal resource/command seam; prototype OpenGL ES 3 /
  WebGL2 *only if* CPU budgets fail, keeping the CPU backend as fallback and test oracle;
  suspend/resume, offline/transient-error and reconnect behavior.
- **Studio** — environment-aware content bundles, prefab/scene composition, import/license
  metadata.
- **Time-based clauses that no amount of code can shorten:** clause 5 requires the pilot to
  meet availability/latency/error/security/cost guardrails for **four consecutive weeks**;
  clause 8 requires the reference game to complete **two measured content/LiveOps
  iterations**.

### Horizon 3 — external developer beta (conditional)

Three external teams complete the golden path unassisted; two publish a second verified
iteration within 30 days; upgrade/migration works across the previous supported version;
support/security/cost/doc-freshness stay within declared capacity for eight weeks.

### Horizon 4 — curated creator ecosystem (conditional, triggered)

Starts only when H3 passes **and** ≥5 external projects publish ≥2 verified releases **and**
≥3 show repeat-player behavior over eight weeks **and** a named owner + budget exist for
moderation and abuse. Scope: curated project pages, publisher identity, content
rating/provenance/takedown/appeal, search and editorial collections.

### Explicitly deferred / conditional

Public self-service submission · sponsored discovery · in-game commerce · creator payouts ·
browser authoring — each gated behind its own evidence, none promised.

---

## 10. Known gaps

Beyond the roadmap's own list, these are structural and worth an agent's attention:

1. **The OpenAPI contract has only one SDK consumer.** Chapter 142 generates
   `/openapi.json` from the same route table Drogon registers and tests their parity, but
   `sdk/` still contains only `cpp/`; there is no TypeScript SDK or cross-SDK suite.
2. **The ADR index covers implemented decisions, not every prerequisite.**
   `docs/adr/README.md` points to the chapters that hold the arguments and records
   reversals, but `RenderDevice` and a true resource-ID model still need decisions.
3. **Resource identity is a content hash, not a resource ID model.** Schema versioning and a
   true dependency graph are still missing; the BaaS asset registry is *content storage*, not
   the immutable artifact/release registry the architecture calls for.
4. **The Studio authoring core is real, but its scene canvas is still narrow.** Undo,
   recovery, asset creation/browser, validation and four workspaces exist; pan/zoom,
   multi-select, copy/paste and Spawner/OnOverlap inspectors remain deferred.
5. **Breadth vs product**: five manifest games reach the Collection and shared release
   spine, but their interaction quality and visual identities are uneven and largely
   untested by hands on native/mobile screens.
6. **Operating evidence is absent.** Horizon 2 says "operate a real small game", but there
   has been no deployment and no player. The four-week guardrail clock has not started.

---

## 11. Decision rules for an agent

Apply these in order when choosing or scoping work.

**Rule 1 — Does the golden path use it?** Platform breadth does not advance a horizon
unless the canonical loop consumes it (roadmap rule 5). A new subsystem that is only
reachable via its own `--flag` is *motion without connection*.

**Rule 2 — Build vs integrate** (`docs/strategy/02`):
- **Build** (it teaches or differentiates): deterministic engine/simulation cores; resource
  IDs, versioned formats and migrations; the project manifest and golden-path CLI; Studio
  workflow and exact packaged preview; the API domain contract; local BaaS behavior;
  reference games, guidebook, failure drills.
- **Integrate** (commodity or operationally specialized): TLS, edge proxy, DNS, CDN;
  PostgreSQL, object storage, secrets store, container runtime; metrics/log/trace storage
  and alerting; email/SMS, payments, store receipts; voice, anti-cheat, malware scanning,
  moderation; Kubernetes/GameLift/Agones-class orchestration; external stores.
- Integration still requires owned adapters, tests, failure behavior, cost budgets and an
  exit plan. *"Use a vendor" is not an architecture.*

**Rule 3 — Do not prioritize now:** a new standalone game genre without platform-journey
coverage; more BaaS endpoints without contract foundations; microservice decomposition;
a custom global game-server scheduler; broad 3D importer/shader parity; SDKs for every
engine before TypeScript; public marketplace, virtual currency, payouts, sponsored
discovery; AI-generated asset publishing without provenance.

**Rule 4 — Prefer closing a verification gap over adding a feature.** Three claims in §8 are
unproven. Converting an ❌ to a ✅ is usually worth more than a new subsystem, because the
roadmap's gates are evidence-based and unproven claims cannot be spent.

**Rule 5 — Respect the blend.** If the last slice was plumbing, a hand-written runtime slice
is legitimate next — and vice versa. Do not stack five plumbing slices in a row on a solo
maintainer, and do not stack five isolated runtime slices either (that is exactly the §10b
failure).

**Rule 6 — Every feature earns a chapter.** The guidebook is not documentation debt; it is
the deliverable of the learning goal. A subsystem without `docs/book/NN-*.md` is unfinished.

**Rule 7 — State what ran and name what did not.** The project's documentation style is to
separate verified from written (see chapter 107's "What is verified, and what is not"). An
agent must preserve this honesty rather than smoothing it into a confident claim.

---

## 12. Working conventions

**Git.** `main` holds stable, reviewed checkpoints. Work happens on **one feature branch per
milestone**; each build step is its own commit; branches merge back with `--no-ff` so merge
commits mark milestone boundaries. **Never commit or push unless asked.**

**Identity.** This is a personal repository owned by `az9thongnguyen`. The machine's *global*
git identity is a work email, so this repository sets a **local** identity
(`az9thongnguyen <az9thongnguyen@gmail.com>`) and pushes through the SSH alias
`github-az9`. On 2026-09-03 all 282 commits were rewritten with `git-filter-repo --mailmap`
to correct historical misattribution. A corporate pre-push secret scanner runs on this
machine; false positives are cleared with narrow local `secrets.allowed` patterns, never
with `--no-verify`.

**Tests.** Every logic library gets a `test_*` target that runs without a window. Some
`test_*` targets *compile* `renderer2d.cpp`/`assets.cpp` directly instead of linking a lib,
to stay dependency-free; tests needing asset files receive `-DASSET_ROOT=...`. Note the
deliberate quirk: `renderer3d.cpp` and `ui.cpp` reference `Renderer2D` symbols but do not
link it — the final target provides them.

**Chapters.** One chapter per shipped feature, numbered sequentially in `docs/book/`. The
chapter explains the *why* and the trade-off, and names its ceiling.

**Simplification markers.** Deliberate shortcuts are marked with a `ponytail:` comment that
names the ceiling and the upgrade path, e.g. *"three sections and nothing more — a dock
manager comes when a second author needs one."* Treat these as intentional, not as TODOs to
opportunistically expand.
