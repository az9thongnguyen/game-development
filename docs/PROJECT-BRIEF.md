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
| `docs/book/` (108 chapters) | The **explanation** of every subsystem, one chapter per feature | Before modifying a subsystem you did not write |
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
   `--hub` and `--hub-ui` cannot drift.
9. **`CMakeLists.txt` lists sources explicitly** — no globbing, on purpose.
10. **A "production" claim requires evidence**: migration, security, observability,
    backup/restore, rollback, and named operating ownership (roadmap rule 4).

---

## 3. Scale and shape of the repository

| Measure | Value |
|---|---|
| Commits on `main` | 282 |
| C/C++ source | ~28,900 lines (`src/` 14,182 · `baas/` 5,203 · rest `sdk/`, `server/`, `tests/`) |
| Test suites | **61, all passing** (~77 s), fully headless — no window or display needed |
| SDL-free static libraries | 26, each with a matching `test_*` target |
| CLI modes in one `demo` binary | 29 flags + a default mode |
| BaaS HTTP routes | 44, plus `/v1/ws`, `/metrics`, `/healthz`, `/dashboard` |
| Guidebook chapters | 108 (`docs/book/00`–`107`) |
| Strategy documents | 6 (`docs/strategy/01`–`06`) |
| CI | GitHub Actions, ubuntu + macOS, clean checkout + golden-path smoke |

### Repository layout

```
src/platform/    the fixed platform seam (platform.hpp) + backend_sdl.cpp
src/engine/      hand-written core: math, renderer2d, renderer3d, geometry, camera,
                 assets, image, text, ui, ecs/, jobs/, memory/, physics/, anim/, fx/,
                 audio/  +  the platform spine: project/, resource/, release/, hub/
src/games/       one directory per scene/tool (chess, fps, viz3d, iso, editor, colony,
                 studio, sandbox, maplab, hub, studio_shell, fx, light, audio, anim, runner)
src/main.cpp     mode dispatch + the launch_entry seam
tests/           61 dependency-free suites
baas/            Drogon Game-BaaS backend (separate process, links no engine code)
sdk/cpp/         gbaas C++ SDK — native libcurl / web emscripten_fetch, one API
server/          hand-written HTTP server (POSIX sockets), serves the WASM build
web/shell.html   the Emscripten shell; ?mode= selects the scene without recompiling
docs/            book/ (108 chapters), strategy/ (6), guides/ (2), superpowers/{specs,plans}
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
| `--3d` | Software-rasterized 3D core: cube, sphere, grid, shading modes |
| `--viz3d` | Interactive 3D sandbox: spawn, mouse-pick select, drag-transform, orbit/pan/zoom |
| `--iso` | Isometric farm sim: tile map, depth sort, small ECS, A* pathfinding, save/load |
| `--editor` | Immediate-mode GUI + physics sandbox |
| `--colony` | **Integration game** standing on the whole engine core (ECS + jobs + frame allocator + asset cache + GUI) and on the BaaS via the SDK |
| `--studio` | **Texture Lab**: hand-written seamless noise (value/Perlin/fBm), `.hrt` export with re-editable recipes, animated sheet export |
| `--sandbox` | Declarative 2D sandbox: drag-drop actors, data-only behaviors, an `OnOverlap` rule, deterministic tick, Play/Stop via scene snapshot |
| `--maplab` | Tile-grid level editor: paint, flood-fill, player spawn placement, `fpsmap1` format |
| `--fx` `--light` `--audio` `--anim` | Playgrounds for particles, 2D lighting, the mixer, sprite animation |
| `--hub` `--hub-ui` `--shell` | The platform surfaces (see §6) |
| `--runner` | Headless BaaS test-run worker |

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
  and the windowed `--hub-ui`/`--shell`, so CLI and window cannot drift.
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
| Scene visual output | ⚠️ mostly manual visual accept. Two structural golden tests now exist (`test_ui_golden`, `test_shell_golden`); the rest is eyeball. |
| Studio shell renders correctly | ✅ verified 2026-09-04 by offscreen render at the real 1280×720×2, inspected as an image. **The window itself was not opened** — screen capture is unavailable here, so SDL's `present()` path is untested. |
| Studio frame cost | ✅ measured 2026-09-04 via `--bench-ui`, **Debug** build: ss=1 median 1.24 ms, ss=2 median 4.63 ms / p95 5.04 ms against an 8 ms budget. No Release measurement. |

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
