# Project Brief — orientation for an AI agent

**Audience:** an AI coding agent (or a new human maintainer) that must understand this
repository well enough to *choose the right next piece of work*, not merely to edit a file
it was pointed at.
**Last verified:** 2026-09-04, against commit `81d3a81` (`main`, 282 commits).
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
| `docs/book/` (117 chapters) | The **explanation** of every subsystem, one chapter per feature | Before modifying a subsystem you did not write |
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
| Commits on `main` | 282 |
| C/C++ source | ~28,900 lines (`src/` 14,182 · `baas/` 5,203 · rest `sdk/`, `server/`, `tests/`) |
| Test suites | **71, all passing** (~10 s), fully headless — no window or display needed |
| SDL-free static libraries | 32, each with a matching `test_*` target |
| CLI modes in one `demo` binary | 29 flags + a default mode |
| BaaS HTTP routes | 44, plus `/v1/ws`, `/metrics`, `/healthz`, `/dashboard` |
| Guidebook chapters | 117 (`docs/book/00`–`116`) |
| Strategy documents | 6 (`docs/strategy/01`–`06`) |
| CI | GitHub Actions, ubuntu + macOS, clean checkout + golden-path smoke |

### Repository layout

```
src/platform/    the fixed platform seam (platform.hpp) + backend_sdl.cpp
src/engine/      hand-written core: math, renderer2d, renderer3d, geometry, camera,
                 assets, image, text, ui, ecs/, jobs/, memory/, physics/, anim/, fx/,
                 audio/  +  the platform spine: project/ (manifest + inspect),
                 resource/, release/, hub/, commands/, document/, tilemap/
src/games/       one directory per scene/tool (chess, fps, viz3d, iso, editor, colony,
                 studio, sandbox, maplab, hub, studio_shell, farm, fx, light, audio,
                 anim, runner) — studio_shell holds the Workspace interface + its two
                 implementations and the full-screen WorkspaceHost that --lab scene uses
src/main.cpp     mode dispatch + the launch_entry seam
tests/           71 dependency-free suites
baas/            Drogon Game-BaaS backend (separate process, links no engine code)
sdk/cpp/         gbaas C++ SDK — native libcurl / web emscripten_fetch, one API
server/          hand-written HTTP server (POSIX sockets), serves the WASM build
web/shell.html   the Emscripten shell; ?mode= selects the scene without recompiling
docs/            book/ (117 chapters), strategy/ (6), guides/ (2), superpowers/{specs,plans}
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
| `--lab map` | Tile-grid level editor: paint, flood-fill, player spawn placement, `fpsmap1` format |
| `--lab fx` `light` `audio` `anim` | Playgrounds for particles, 2D lighting, the mixer, sprite animation |
| `--hub` `--shell` | The platform surfaces (see §6) |
| `--runner` | Headless BaaS test-run worker |

Twelve of those labs used to be twelve top-level flags, and `--hub-ui` was a second
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
ctest --test-dir build --output-on-failure          # 61 suites, headless
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

---

## 8. Verification ledger — what is proven, what is only written

An agent must not upgrade any of these from "written" to "works" without running it.

| Claim | Status |
|---|---|
| 62 test suites pass | ✅ **verified** 2026-09-04 (61 before chapter 108 added `shell_golden`) |
| Native build | ✅ verified |
| Web (Emscripten) build + served by the hand-written webserver | ✅ verified: build links, all assets return 200 |
| Golden path publish → promote → audit | ✅ verified interactively (three channels + audit log) |
| Backup/restore drill | ✅ verified (chapter 98, executed) |
| SDL2 build split, both directions | ✅ verified natively, and configure inside the Drogon container correctly skips SDL2 |
| **Docker container builds and answers `/healthz`** | ❌ **never verified.** The Drogon base image is `linux/amd64`; building under arm64 emulation exceeds the local time budget. Dependency-complete but unproven. |
| **PostgreSQL production adapter** | ❌ **not implemented/run.** Production is still single-node SQLite. |
| **CI green run** | ⚠️ **unknown from this machine.** `ci.yml` self-documents "written but not run in the authoring environment"; `gh` is unauthenticated here. |
| Purchase affordability check | ⚠️ **known ceiling:** documented TOCTOU — the affordability check and the debit are not under one lock. Recorded deliberately, not accidentally. |
| Scene visual output | ⚠️ mostly manual visual accept. Two structural golden tests now exist (`test_ui_golden`, `test_shell_golden` — the latter now covers six sections, the Project panel's healthy/holed/unreadable states and the audit-log ordering); the rest is eyeball. |
| Studio shell renders correctly | ✅ verified 2026-09-04 by offscreen render at the real 1280×720×2, inspected as an image. **The window itself was not opened** — screen capture is unavailable here, so SDL's `present()` path is untested. |
| Command registry + undo/autosave, **and a Studio that uses them** | ✅ verified 2026-09-04 (chapters 111–112, extended 116): 71 tests. `--cmd` runs real operations, the old flags are aliases onto it, and `Cmd+K` in the Studio lists the same registry. The Map workspace pushes every edit onto `CommandStack`, autosaves on a timer and offers recovery on open — the "no consumer" gap from chapter 111 is closed. As of chapter 116 there are **two** workspaces (Map and Scene) behind a `Workspace` interface, and `--lab scene` (then `--sandbox`) runs the same Scene workspace full-screen. |
| Shared 2D map format (`map2`) + fpsmap1 migration | ✅ verified 2026-09-04 (chapters 110, 112): `--fps` reads the real authored level through the new path, `test_fps` asserts it is identical grid-for-grid to the legacy parser, and the Studio's Map workspace now **renders and edits** a map2. `test_map_workspace` ends by reading a file the editor wrote with the raycaster's own loader. Building that consumer found a real hole: `to_text`/`load` could not round-trip a tiles layer with no tileset — the shape an editor produces before there is art. ⚠️ Map Lab (now `--lab map`, then `--maplab`) still writes fpsmap1 and has not been absorbed; entities and triggers have no editing UI. |
| UI layer: keyboard, focus, clipping, modals, text editing | ✅ verified 2026-09-04 (chapter 109): 62 tests including a full text editor and two mutation checks; confirmation screen rendered offscreen with its negative control. ❌ **the window itself has still never been opened**, and resizing has never been performed. |
| A second game reaching the platform through a manifest | ✅ verified 2026-09-04 (chapter 113): `projects/farm.gameproject` added a game with a manifest and a scene and **nothing else changed** — `--project`, `--project-inspect`, `--project-package` and `--hub` all work on it unmodified. The farm consumes `map2` layers + entities, `Camera2D`, `save_core` and the Studio's Map workspace. Its first use of `Camera2D` found a real bug: `set_viewport` did not re-clamp, so a world smaller than the window was not centred on the first frame (and a resize would push the view off the world). ❌ **never played in a window** — only driven by synthesized input, so step timing and day length are unmeasured *feel* questions. ❌ **no art**: flat colours and circles. |
| One project resolve, shared by every verb | ✅ verified 2026-09-04 (chapter 114): `engine::inspect()` replaced **four** hand-written copies of read+validate+hash (CLI launch, CLI inspect, publish, hub) and a fifth partial one in the Studio. They had already drifted: publish returned at the **first** missing asset while the other three listed all, so `--project-publish` reported one broken path per run. Verified at the CLI, not only in a unit test — a probe manifest with three broken paths now yields the same three lines from publish and inspect. Four mutation checks (first-problem-only, dropped missing assets, hashing an incomplete project, reordered problems) each break `test_inspect`. |
| Studio Project section (asset browser + validation) | ✅ verified 2026-09-04 (chapter 114): draws the same `engine::inspect` answer `--project-inspect` prints — type, path, content hash, size, present/missing per declared asset, plus the verdict and the package hash this source would publish as. Building it found a real divergence: the scene held its own `known_entries = {"fps"}` while `main.cpp` knew `{"fps","farm"}`, so `--shell projects/farm.gameproject` called the farm project broken while the CLI called it shippable. The list is injected now, with a negative control proving the list is what makes the difference. ❌ **never clicked** — offscreen renders only. ❌ never held a long list (5 assets, no scrolling exercised at scale); nothing watches the filesystem, so `R`/Re-inspect is manual. |
| Operational evidence: the audit log, in a window | ✅ verified 2026-09-04 (chapter 114): `engine::log()` has returned the append-only history as data since the release store existed and **no window had ever drawn it**. Both hub surfaces now do, through the one shared panel. Newest-first, UTC, with the operator's reason as the widest column — pinned by a row-difference test that inverts if the loop is reversed and fires if the block is deleted. Rendering it immediately exposed a real `publish` entry with a **blank reason**, written before D17 existed. ⚠️ no filter and no paging: `engine::log(channel)` supports the first, the panel does not ask for it. |
| Play viewport: a game running inside the Studio | ✅ verified 2026-09-04 (chapter 115): the scene gets its **own** framebuffer at the game's native size — the alternative (drawing into the Studio's under a clip) would make every coordinate a lie, since a scene asks the renderer how big the screen is. Pause and Step-one-frame are why it beats launching the game. Proven twice: a probe scene that counts its own ticks (six mutations break it), then a real `farm::FarmScene` running 180 fixed steps inside a real `StudioShellScene`, with the nav rail asserted untouched. Writing the test found a real bug: "receiving no input" was a default `InputState`, whose mouse sits at (0,0) — a real position, so an unfocused game was told the pointer was parked in its top-left corner. ❌ **still never clicked**. ❌ the mouse does not reach the game at all (deliberate: a half-correct pointer is worse than none). ❌ only `farm` has been played; `fps` is in the table and untested there. ⚠️ the viewport keeps running on other sections — deliberate, but an expensive scene costs frame time in the Map workspace with no warning. |
| One entry table (launch · validate · play) | ✅ verified 2026-09-04 (chapter 115): `launch_entry` was a chain of `if`s with a `kKnownEntries` literal beside it and a comment asking a human to keep them in sync. The Play viewport would have been the third reader, so the three collapsed into one `entries()` table with `known_entries()` derived from it. A game can no longer be launchable-but-unknown or known-but-unlaunchable. |
| Fixed-timestep clock, shared not copied | ✅ verified 2026-09-04 (chapter 115): extracted from `App::frame` into header-only pure `engine::FixedStep` so the Play viewport runs on the same clamp rather than a second one that agrees on ordinary frames and diverges when the machine stalls. Four mutations break `test_fixed_step`, including the exact truncation bug the farm clock had in chapter 113. |
| A second workspace, and the interface it earned | ✅ verified 2026-09-04 (chapter 116): chapter 112 deliberately shipped one workspace and NO interface ("a shape with one occupant"). The second one is the absorbed sandbox, and having two changed the shape before the second existed: `status()`/`hint()` moved onto the workspace (a scene has no tiles, so the shell had to stop knowing what document it held), `inspector_width()` became a request the shell overrules, and recovery became defaulted rather than pure. Eight mutations break the suite. ⚠️ still no pan/zoom, multi-select or copy/paste in the scene canvas; Spawner/OnOverlap round-trip but have no inspector. |
| `--lab scene` (was `--sandbox`) absorbed, not reimplemented | ✅ verified 2026-09-04 (chapter 116): `sandbox_scene.{hpp,cpp}` deleted (315 lines); it is a `WorkspaceHost` around the same object the Studio's Scene tab holds. What the sandbox gained without anything being written for it: undo (it had none), autosave + recovery, the command palette, the post-chapter-109 widgets, input handling in `update()` instead of `render()`. The host path is covered headless, including that `scene.play`/`scene.undo` are registered there. ❌ Map Lab still exists and still writes `fpsmap1`; `WorkspaceHost` is the mechanism that would retire it. Chapter 120 took the CLI-surface decision (it is `--lab map` now) but did NOT absorb it — it is still the only place entities and spawns can be edited. |
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
| CLI surface: one door per kind | ✅ verified 2026-09-04 (chapter 120): 31 flags → three kinds. A **game** is launched from its manifest, the **Studio** is `--shell`, and every other windowed thing is a **lab** behind `--lab [id]`, derived from one table the way `entries()` already was. `--hub-ui` deleted outright along with `hub_scene.{hpp,cpp}` (the Studio's Hub section draws the same panel from the same view model); twelve flags folded, nothing removed that was the only door to a room — Map Lab is still the only entity/spawn editor, and seven labs are the only runtime consumers of `particles_core`, `light_core`, `audio_core`, `tween_core`, `render3d_core`, `viz3d_core`, `studio_core`. An unknown flag used to fall through to the M0 demo **silently**; it is now an error carrying the full list plus a retired-flag map, and `demo --help` prints the same. ❌ **no test covers the CLI surface** — CI's smoke script covers the spine verbs only; `--lab` and `--help` were checked by running them. |
| Reading a format we did not invent | ✅ verified 2026-09-05 (chapter 121): hand-written DEFLATE (`inflate_core`, RFC 1951+1950 — stored/fixed/dynamic blocks, LZ77, verified Adler-32) and a PNG decoder on top of it (`png_core`: depth 8, colour types 0/2/3/6, tRNS, all five filters, non-interlaced; everything else refused **by name**). Decode only — nothing writes a PNG, and a compressor has no consumer here. Tested against streams a **real zlib** produced at levels chosen so all three block types appear, plus four corruption modes that must be refused rather than decoded into rubbish. Three PNG fixtures are hand-built so their pixels are known by construction (one row per filter, a tRNS shorter than its palette, greyscale); the fourth and fifth are files this project did not make. ❌ no encoder, no interlace, no 16-bit. |
| Foreign art has one door | ✅ verified 2026-09-05 (chapter 121, extended in 122): `--cmd asset.import <src.png> <dst.hrt>` is offline — the engine never decodes PNG at runtime — so an open-licence pack and art drawn in the Texture Lab arrive downstream as the same format, and the asset cache, resource closure and package hash have one kind of file to think about. `assets/ATTRIBUTION.md` records source, author, licence and the command that reproduces the import; the source PNG is committed next to the `.hrt` so the import can be re-run rather than taken on trust. Four `register_release_commands` call sites became one `cmd::register_all`. ⚠️ the 5 KB import source ships in the web preload, deliberately. |
| The farm has art | ✅ verified 2026-09-05 (chapter 121): Kenney Tiny Town (CC0, 16 px, 12 × 11). `tilemap::Tileset` cuts the sheet once at load; the map keeps **semantic** ids and `assets/farm/theme.def` joins id → sheet index, so swapping art is a new sheet and at most a new theme file — no rebuild, and no renumbering the level. An id with **no line has no art** and falls back to the flat colour, which is what makes "support both" per-tile: Tiny Town has no water tile, so the pond stays flat while everything around it is themed, and the test states that in pixels. Verified in a browser, with a mouse click tilling a themed tile. ⚠️ two mutations survived at first because `draw_tile`'s guards masked each other — fixed by having one condition, not three. ❌ no autotiling (grass meets dirt with a hard edge), one sheet per map, and `map2`'s own tileset field is still unused. |
| Art this project drew, in the same pipeline | ✅ verified 2026-09-05 (chapter 122): the second half of the "support both" decision. `--cmd asset.texture <src.recipe> <dst.hrt>` bakes a Texture Lab recipe to `.hrt` offline, so a tile from a CC0 pack and a tile the project made are the same kind of file downstream. The theme now names its sheets and may declare several, which is what keeps our art OUT of the imported file — compositing into `town.hrt` would make the derived image impossible to attribute and would be erased by the next re-import. The farm's pond is the first tile through the door: 16×16, two shades of blue, `assets/textures/farm_water.{recipe,hrt}`. `test_farm` regenerates it from the committed recipe and compares **bytes**, so "drawn in the Studio" is checked, not claimed — and that also pins the generator's long-standing purity claim. ⚠️ the recipe parser is deliberately tolerant (unknown keys keep defaults), which behind a *writing* command would have baked the DEFAULT texture over the destination and reported success; it now reports how many keys it recognised and the command refuses zero. ⚠️ one mutation survived: the sheet range check had never been reached, because no file in the repo has a wrong index — fixed by a test on a **copied** asset tree, not by deleting the guard. ❌ still no autotiling (the pond is a hard-edged rectangle), one tile repeated with no variants, the water does not move although `make_sheet` could animate it for free, and the Texture Lab can generate a texture but cannot draw a SHAPE — so "clone and redraw" is proven for textures only. |
| Somewhere to draw | ✅ verified 2026-09-05 (chapter 123): a third `studioshell::Workspace` — a pixel editor over `.hrt`, with pencil/rect/fill/eyedropper, undo through the same `doc::CommandStack` the map and scene use, autosave and recovery on a BINARY document, and a palette **sampled from the open image** rather than a fixed ramp. It takes a LIST of the project's textures because the farm's first texture is the imported Kenney sheet, which is the one file our own pixels must never enter. `pixel.next` and the inspector list are one operation (`open_index`), which REFUSES to switch while dirty — there is no undo across a reload. Tested headless like the map workspace, and the claim it pins is the ROUND TRIP: paint, undo across two different previous colours, save, then read the file back and DECODE it. Verified in a browser running WASM. ⚠️ it exists but nothing has been drawn with it yet — the narrow path pieces that motivated it are still not in the sheet. ❌ no colour picker (you can only paint colours the image already contains), one layer, no selection/move/copy, no canvas resize, tile guide fixed at 16 px. |
| Keyboard input on the web | ✅ verified 2026-09-05 (chapter 123) — and ❌ **before** it, silently, since chapter 118. SDL2's Emscripten backend listens for keys on the CANVAS element; the page never focused it, so `document.activeElement` was BODY and no keydown ever reached SDL. Every web build shipped with NO keyboard: no WASD in the farm, no F5/F9, no chess input, no Studio shortcuts. It survived three chapters of browser verification because every check used the **mouse**, which SDL registers on the canvas the page passes in `Module.canvas`. Fixed with `tabindex="0"` plus `focus()` on runtime-init and on mousedown. Now verified by holding a key: `R` switches the pixel editor's tool, `D` walks the farm's player east. Behind it sat a second bug it had been hiding: the command modifier was a compile-time `#ifdef __APPLE__` in FOUR files, so the wasm — compiled once, run on every OS — made a Mac user in a browser press Ctrl+S. `InputState::accel()` is now one runtime definition accepting either modifier; measured in the browser, a stroke changes 220 screen pixels and Cmd+Z returns it to 28 (the moved hover cursor). ⚠️ key edges are poll-derived, so a synthesized press+release inside one 16 ms frame is invisible — a driver must HOLD the key, and the first three attempts at this test failed in a way indistinguishable from the bug. |
| Playable by hand | ✅ verified 2026-09-05 (chapter 124): a d-pad and two action buttons over the farm, driven by a REAL touch in a 390×844 phone viewport — holding `>` walks the player east. It reads the POINTER, not touch events, because SDL synthesizes a mouse from a finger: one implementation for tap/click/trackpad, no new event type at the platform seam, and every existing mouse-driven surface (the whole Studio) keeps working on a touch device. ONE `layout()` answers the renderer and the hit test, so a control cannot be drawn in one place and hit in another — the one bug a screenshot cannot show. `consumed` is set by POSITION, not by the button being down, so a tap on the pad does not also till the tile beneath it. Fitting is decided by PROPORTION (at most half the width, two fifths of the height), not pixel thresholds — the first version drew the pad over the 480×270 retro framebuffer. ⚠️ **one finger at a time**: no holding a direction while tapping an action. ⚠️ always drawn, never auto-hidden — there is no reliable way to ask whether a device has touch. ❌ tools (1–4) and save (F5/F9) have no buttons; only the farm has any of this. |
| Translucent outlines | ✅ fixed 2026-09-05 (chapter 124) — ❌ **broken and invisible since chapter 69**. `draw_round_rect` painted its four straight edges with an opaque copy and its four corner arcs with the alpha-respecting sink, so ONE call produced solid edges joined by faint curves; `draw_rect` was silently opaque too, and neither has a `_blend` sibling a caller could pick instead. It survived because every outline in the project was opaque until a d-pad wanted a faint one. Opaque still takes the fast path, and `test_aa` now pins both directions plus `fill_round_rect` (which was already correct). |
| Studio frame cost | ⚠️ **the recorded number was wrong, corrected 2026-09-04 (chapter 117).** Every earlier entry reported "Release ss=2 1.1–1.6 ms" — that was the **ss=1** column. Re-measured, and cross-checked by building the previous commit (`eaa54b4`) in Release and running the same bench: **ss=1 1.0–1.8 ms · ss=2 6–10 ms**, with ss=2 **over the 8 ms budget in 2 of 3 runs**. There is no regression from this slice; the figure had been mislabelled since it was first taken. So the honest statement is: at 1280×720 the Studio is comfortable, and **with supersampling on it is at or past budget** — fill-bound, as the ratio always said (ss=2 ≈ 6× ss=1 at 4× the pixels). ⚠️ `--bench-ui` measures the Studio with **no game running**; a Play viewport rendering a 640×360 scene every frame is not in that number. ⚠️ p95 swings from 2 ms to 90 ms run to run on this laptop, so only medians and ratios are dependable. |

---

## 9. What is planned

### Remaining in Horizon 2 (exit gate has 8 clauses; see `docs/strategy/04`)

- **Segments and experiments** — versioned segments, scheduled configuration/events,
  experiment assignment, exposure events, guardrails, stop/rollback. *This is the largest
  untouched area* and gates clause 6 (exposure/outcome data quality).
- **PostgreSQL production adapter** while retaining SQLite local mode.
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

1. **No OpenAPI/contract specification.** The API is implementation-defined. This is ranked
   priority #4 in `docs/strategy/02` and blocks a TypeScript SDK and any SDK-conformance
   testing. `sdk/` contains only `cpp/`.
2. **No ADR directory**, although `docs/strategy/03` §"Architecture decisions required
   before implementation" lists several ADRs as prerequisites (project manifest semantics,
   resource IDs, `RenderDevice`, API contract).
3. **Resource identity is a content hash, not a resource ID model.** Schema versioning and a
   true dependency graph are still missing; the BaaS asset registry is *content storage*, not
   the immutable artifact/release registry the architecture calls for.
4. **The Studio shell is a frame, not an authoring product** — nav rail + panels. No undo,
   no recovery, no asset browser, no validation surface. The Labs are still separate scenes.
   *(Partly addressed 2026-09-04, chapter 108: it now draws through `ui::Context` and the
   design system, the Hub is one shared panel driven by mouse or keyboard, and it is
   covered by `test_shell_golden`. What remains is the authoring half — undo, documents,
   asset browser, validation — which is S4/S6 of `docs/new-plan/`.)*
5. **Breadth vs product**: 29 CLI modes, but only **one** (`fps`) is reachable as a project
   entry through `launch_entry`.
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
