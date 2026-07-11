# Current State and Capability-Gap Analysis

**Repository baseline:** authored against commit `ad7349b`; re-baselined on adoption to
HEAD `9cedf29` (2026-07-11). The layer inventory below is written against `ad7349b`;
[§10](#10-additions-since-the-strategy-baseline-2026-07-11) records what changed since.
**Validation baseline:** native Debug build succeeded; all CTest suites passed when
integration tests were permitted to bind localhost (38 at `ad7349b`, more since).
**Purpose:** Separate implemented learning value from product and production maturity,
then identify the smallest gaps that block the canonical product loop.

## Maturity model

This document avoids a single “done/not done” label. A subsystem can be an excellent
learning implementation and still be unsafe to operate for external users.

| Label | Meaning |
|---|---|
| **Proven learning implementation** | The mechanism exists, is explained, has meaningful tests or a visible demo, and teaches the intended concept |
| **Functional prototype** | The feature works for the repository owner and reference demos but lacks a supported product workflow or complete failure handling |
| **Production seam** | A boundary exists that can accept a production adapter without rewriting consumers; the production adapter may not exist |
| **Production-capable** | Deployment, migration, security, observability, backup/restore, compatibility, rollback, and operating ownership are evidenced |
| **Absent** | No supported implementation or seam is present in the repository baseline |

No current area is labeled “production-capable” as a whole. Individual mechanisms
such as parameterized SQL or password hashing are production-oriented, but a service
is only as ready as its complete operating lifecycle.

## Executive assessment

The repository is technically broad and unusually coherent for a learning project.
Its engine boundaries, deterministic core libraries, WebAssembly posture, authoring
experiments, BaaS modules, tests, and guidebook are real assets. The principal gaps
are product integration and operational maturity:

1. There is no project model or single supported workflow joining the layers.
2. Authoring tools are separate scenes rather than one resource-aware Studio.
3. Web delivery is a build target, not an immutable publish/release capability.
4. The API is implementation-defined and the SDK is effectively C++-only.
5. The backend demonstrates many services but lacks a complete production lifecycle.
6. The documentation is deep by subsystem but not organized around user outcomes.

The correct next step is to connect and harden what exists, not to maximize module
count.

## Layer-by-layer inventory

### 1. Platform and application loop

**Evidence**

- `src/platform/platform.hpp` defines the engine-facing platform boundary.
- `src/platform/backend_sdl.cpp` owns SDL interaction.
- `src/engine/app.*` implements variable render timing with fixed update steps.
- `src/engine/scene.hpp` defines the scene contract and render context.
- `src/main.cpp` selects scenes through command-line arguments.
- `CMakeLists.txt` builds native and Emscripten targets from shared engine/game code.

**Maturity:** Proven learning implementation; production seam for platform backends.

**What is strong**

- The tick architecture is compatible with browser event-loop constraints.
- Game code does not need SDL types.
- Headless core libraries make deterministic testing practical.

**What blocks the platform loop**

- Scene selection is program dispatch, not a versioned project configuration.
- There are no named development/preview/release build profiles.
- Windows/Linux behavior and packaging are not evidenced by CI artifacts.
- Touch, gamepad action mapping, accessibility preferences, suspend/resume, and
  device-capability policies are not defined as product contracts.

**Disposition:** Build the project/build-profile contract; integrate platform-specific
packaging tools; defer console certification.

### 2. Engine runtime and rendering

**Evidence**

- `src/engine/renderer2d.*`, `renderer3d.*`, `pipeline.hpp`, `geometry.*`, `camera.*`,
  and `math.hpp` implement the renderer and transform pipeline.
- `src/engine/ecs/`, `jobs/`, `memory/`, and `physics/` provide reusable core systems.
- `src/engine/assets.*`, `asset_cache.*`, and `image.*` centralize resource I/O and
  caching.
- `src/engine/ui/` and `src/engine/text/` provide immediate-mode UI and font systems.
- Tests such as `tests/test_render3d.cpp`, `test_aa.cpp`, `test_ecs.cpp`,
  `test_physics.cpp`, and `test_ui_golden.cpp` exercise headless behavior.

**Maturity:** Proven learning implementation; functional prototype as a reusable game
runtime.

**What is strong**

- The CPU framebuffer makes algorithms inspectable and native/Web behavior aligned.
- Core libraries have intentionally narrow dependencies and test targets.
- Asset and frame-loop seams support later backends without leaking platform details.

**What blocks broader game creation**

- No canonical scene/resource/prefab serialization across games.
- No stable resource IDs, dependency graph, migration framework, or import database.
- No accelerated render-device boundary, materials/shaders, skeletal animation, or
  production content pipeline.
- Audio is a platform capability rather than a mixer/spatial/gameplay system.
- Profiling, frame budgets, memory budgets, and compatibility baselines are not part
  of release acceptance.

**Disposition:** Build resource identity, scene/prefab, animation, audio, and a
`RenderDevice` seam in reference-game order. First accelerated backend should follow
the existing OpenGL ES 3/WebGL2 direction; monitor WebGPU. Do not pursue AAA rendering
parity.

### 3. Games and reference experiences

**Evidence**

- `src/games/chess/` proves rules, AI, GUI, and TUI.
- `src/games/fps/` proves raycasting, maps, textures, collision, and audio.
- `src/games/viz3d/` proves reusable 3D scene manipulation and picking.
- `src/games/iso/` proves tilemaps, pathfinding, save/load, and isometric composition.
- `src/games/colony/` integrates ECS, jobs, assets, UI, BaaS, and replay concepts.
- `src/games/editor/`, `studio/`, `sandbox/`, and `maplab/` prove editing workflows.

**Maturity:** Proven learning implementations; functional prototypes as product
templates.

**What is strong**

- The examples exercise different subsystems instead of repeating one genre.
- Colony is a valuable cross-layer integration seed.
- Each milestone is tied to design/plan/guidebook material.

**What blocks their use as platform contracts**

- Games are modes in one `demo` executable rather than independent versioned projects.
- There is no declared compatibility matrix or upgrade test across project versions.
- Reference games do not have release manifests, packaged assets, telemetry schemas,
  performance budgets, or end-to-end golden-path acceptance.
- There is no clear supported sample versus historical learning milestone distinction.

**Disposition:** Select and maintain three reference games with distinct journeys;
freeze historical demos except for compatibility and critical fixes.

### 4. Studio and content authoring

**Evidence**

- `src/games/studio/` generates deterministic textures and editable recipes.
- `src/games/sandbox/` supports declarative actors, behaviors, rules, snapshots, and
  save/load.
- `src/games/maplab/` begins a tile-grid authoring workflow shared with the FPS map
  model.
- `assets/` contains seeded engine-native content.
- Chapters 73–89 explain procedural generation, tiling, Studio, sandbox, textured
  sprites, tween/easing, 2D lighting, the audio mixer, sprite animation, and spawn
  authoring.

**Maturity:** Functional prototype with proven learning cores.

**What is strong**

- Authoring creates assets consumed by existing game/runtime code.
- Pure edit/generation logic can be tested without a window.
- Formats are deliberately simple and understandable.

**What blocks a coherent Studio**

- Separate command-line scenes duplicate shell, selection, save, and collection UX.
- Native directory scans act as the collection database.
- No project ownership, stable IDs, asset metadata, dependencies, thumbnails cache,
  validation status, or schema migration.
- No shared undo/redo, autosave/recovery, dirty state, multi-document navigation,
  import pipeline, or exact packaged preview.
- Browser persistence is preview-only through in-memory Emscripten filesystem behavior.

**Disposition:** Build a shared Studio shell and resource registry before adding more
Labs. Retain readable formats; add version metadata and migrations rather than moving
immediately to a complex third-party format.

### 5. Web build and distribution

**Evidence**

- `cmake/emscripten.toolchain.cmake` and Emscripten branches compile shared code.
- `web/shell.html` hosts the generated module.
- `build-web` produces HTML, JavaScript, WASM, and data artifacts.
- `server/` and `baas/` can serve the Web build with required MIME types.

**Maturity:** Functional prototype; production seam through asset/platform abstractions.

**What is strong**

- The original architectural bet succeeded: game logic did not require a Web rewrite.
- Browser preview matches the selected C++ scene and can reach the BaaS SDK.

**What blocks publishing**

- Scene selection requires editing shell arguments or URL conventions rather than a
  project release manifest.
- No optimized build profile, size budget, cache policy, compression contract,
  content hash, integrity metadata, or source-map policy.
- No artifact registry, release channel, preview URL, promotion history, or rollback.
- No automated browser smoke test across supported browsers.
- No export preset for existing distributors such as itch.io.

**Disposition:** Build immutable packaging, preview channels, and rollback; integrate
static hosting/CDN and external distribution rather than building a store.

### 6. Game BaaS

**Evidence**

- `baas/auth/`, `leaderboard/`, `cloud_save/`, `inventory/`, `remote_config/`,
  `analytics/`, `live_events/`, `realtime/`, and `replays/` provide service modules.
- `baas/gateway/` implements API key/auth/admin filters and a token-bucket limiter.
- `baas/observability/metrics.*` records request counts and normalized routes.
- `baas/admin/` and `baas/web/dashboard.html` provide operator APIs and UI.
- `baas/db/db.cc` creates SQLite tables for projects, users, scores, saves, inventory,
  config, analytics, events, and replays.
- Integration tests cover service behavior using a real local server.

**Maturity:** Broad functional prototype with several production-oriented mechanisms;
production seam because it is a separate process and SDK boundary.

**What is strong**

- Backend dependencies remain outside the engine runtime.
- Tenant project IDs and filters are present across core flows.
- Password hashing, JWT, parameterized database access, optimistic save versions,
  rate limiting, structured access logs, and integration tests establish good habits.
- Modular-monolith boundaries match the current operating scale.

**What blocks production use**

- Schema creation is embedded in application code; there is no versioned migration,
  downgrade/rollback, or compatibility policy.
- SQLite is appropriate for local learning but not a demonstrated HA/concurrent
  production posture; no PostgreSQL adapter is proven.
- Admin auth is secret-header oriented; no operator accounts, roles, MFA, short-lived
  credentials, rotation workflow, or complete audit trail.
- No refresh-token/session lifecycle, account recovery/linking, privacy export/delete,
  retention policy, abuse reporting, sanctions workflow, or trust-and-safety operations.
- Inventory exposes grant/consume but not a catalog, currencies, atomic multi-item
  transactions, idempotency keys, ledger, receipt validation, or fraud boundaries.
- Analytics is event ingestion plus summary, not a governed schema, warehouse/export,
  cohorts, retention, funnels, experiment exposure, or data-quality workflow.
- Metrics are in-process JSON; no trace context, durable time series, alerting, SLO,
  capacity model, log aggregation, or cost attribution.
- No container/deployment definition, TLS/reference proxy, secrets provider,
  zero-downtime upgrade, backup, restore drill, or incident runbook.
- Realtime is an in-memory hub; no authoritative match simulation, reconnect state,
  regional placement, durable presence, or horizontal coordination.

**Disposition:** Productionize the selected path before new service breadth. Build
game-domain contracts, local adapters, and operating documentation. Integrate SQL,
object storage, secrets, telemetry backends, and dedicated-server orchestration.

### 7. SDK and developer experience

**Evidence**

- `sdk/cpp/include/gbaas/client.h` exposes service handles for auth, leaderboard,
  saves, inventory, config, analytics, events, replays, and realtime.
- `ITransport` and `IWsTransport` separate native libcurl from Emscripten HTTP/WS.
- `Client::update()` preserves the non-blocking game-loop contract.
- Fake transports enable deterministic SDK unit tests.

**Maturity:** Proven C++ learning implementation; functional native/Web SDK.

**What is strong**

- Transport seams mirror the engine's platform seam.
- The callback/pump design respects both native and browser loops.
- Minimal dependencies make the SDK usable by the hand-written runtime.

**What blocks adoption**

- No canonical OpenAPI/schema contract, generated conformance suite, or compatibility
  version policy.
- C++ is the only complete supported SDK; no TypeScript/Web, Godot, Unity, or Unreal
  developer path.
- Hand-built JSON and request bodies can drift from controllers.
- No retry/backoff policy, offline queue, cancellation, request IDs, refresh-token
  lifecycle, telemetry hooks, or full error taxonomy.
- No package manager distribution, semantic versioning, changelog contract, quickstart
  project, or upgrade guide.

**Disposition:** Build the API contract and contract tests first; then build a small
TypeScript SDK and one adapter chosen by external-user evidence. Avoid generating many
unmaintained SDKs.

### 8. Delivery, operations, and governance

**Evidence**

- CMake builds many focused libraries and 38 test targets.
- ASan/UBSan instructions exist.
- Git milestone workflow and feature specs/plans are documented.
- No tracked `.github` workflow, container file, Compose file, deployment manifest,
  OpenAPI document, or migration directory exists in the baseline.

**Maturity:** Proven local engineering workflow; production capabilities absent.

**Key gaps**

- No clean-checkout CI matrix, artifact retention, dependency/license scan, or release
  provenance.
- No supported local stack command or production deployment profile.
- No semantic release/version compatibility policy across runtime, assets, API, SDK,
  and BaaS.
- No ownership map, security response policy, support expectation, or deprecation
  process.
- No operational data protection or recovery evidence.

**Disposition:** Build CI/release contracts and documentation; integrate commodity
pipeline, registry, deployment, scanning, and monitoring services.

### 9. Documentation and learning product

**Evidence**

- `requirements.md` records the original Vietnamese vision and constraints.
- `docs/book/` contains implementation-linked chapters from platform basics through
  BaaS and Studio.
- `docs/superpowers/specs/` and `plans/` retain design and TDD reasoning.
- `CLAUDE.md` concisely explains boundaries and development commands.

**Maturity:** Proven learning implementation; functional contributor documentation.

**What is strong**

- Concepts are explained next to working, testable code.
- Architectural constraints are repeated consistently across major subsystems.
- The project history shows incremental reasoning rather than a code dump.

**What blocks documentation as product UX**

- The guidebook count and current roadmap can drift as new chapters land.
- There is no role-based start path, concept index, supported-version banner, or docs
  build/link checker.
- Operational and product journeys are weaker than subsystem explanations.
- “Done” milestone rows can be misread as production claims.

**Disposition:** Keep canonical subsystem chapters; add journey-based onboarding,
strategy navigation, freshness metadata, link validation, and explicit maturity labels.

## 10. Additions since the strategy baseline (2026-07-11)

Twelve milestones landed between the authoring commit `ad7349b` and the adoption HEAD
`9cedf29`. They matter to this analysis in two different ways, and the split is itself
evidence for the roadmap's central thesis.

### 10a. Additions that partially advance a flagged gap

| Milestone | Gap it touches | Honest status |
|---|---|---|
| BaaS **asset registry** (`/v1/assets`, put/get/list/remove, project-scoped) + SDK `Assets` handle | "Resource identity/versioning" and "resources cannot be safely shared/packaged" | A real step toward server-side asset sharing, but it is **content storage**, not the immutable *release* registry, resource-ID model, dependency graph, or content-hash lineage the architecture calls for. Do not treat it as the artifact store. |
| BaaS **test-runner** (`/v1/testruns` coordinator + `runner_core` headless worker + SDK `TestRuns`) | "Managed validation / CI acceptance" | A genuine seed of managed headless runs, but it is **not** clean-checkout multi-platform CI, release provenance, or a golden-path synthetic check. It validates sandbox scenarios, not a project's golden path. |

These two soften — but do not close — the corresponding capability-gap rows below. The
rows are annotated rather than deleted, because the *product-lifecycle* versions of
these capabilities (immutable release IDs, resource schemas, CI provenance) still do
not exist.

### 10b. Additions that are exactly the pattern the roadmap says to now pause

Particle system, tween/easing, 2D additive lighting, software audio mixer, sprite-sheet
animation (Flipbook), textured walls, sandbox animated actors, studio sheet export, and
Map Lab author-placed spawn. Each is a **proven learning implementation** with headless
tests and a chapter — real craft, and real value under the project's learning goal.

But every one of them is **isolated runtime/authoring depth built ahead of a
golden-path consumer** — the precise definition of ranked priority #10 ("selective
runtime depth… only as demanded by reference games") and of the "breadth before product"
structural risk. None of them is reachable through a versioned project; each remains a
separate `--flag` scene. Under the learning goal this is fine and even admirable; under
the *product* goal it is motion without connection.

**Implication for the adopted _blend_ posture.** This is the concrete reason the owner's
chosen cadence is "one thin golden path first, then interleave." The next unit of work
should not be a thirteenth isolated subsystem; it should be the manifest/CLI/one-game
golden path that finally lets this accumulated depth be created, tested, packaged, and
previewed as *one project*. A hand-written runtime slice may ride alongside each
golden-path slice to keep the work enjoyable — but no longer instead of it.

## Capability-gap matrix

| Capability | Current evidence | User impact of gap | Decision | Timing |
|---|---|---|---|---|
| Project manifest | CLI modes in `src/main.cpp` | No portable project identity or automation contract | Build | Now |
| Unified platform hub | Separate scenes and dashboard | Users must understand repository internals to act | Build | Now |
| Resource identity/versioning | `assets::`, `.hrt`, recipes, serializers | Assets cannot be safely shared, migrated, or packaged | Build | Now |
| API contract | Controllers + hand-written SDK | Drift and additional SDKs are expensive | Build | Now |
| CI/release artifacts | Local CMake/CTest only | No reproducible support claim | Integrate pipeline; build acceptance | Now |
| Web preview publishing | Manual `build-web` and serving | No shareable, promotable, reversible release | Build workflow; integrate hosting | Next |
| Studio shell | Texture/Sandbox/Map scenes | Authoring UX and lifecycle fragment | Build | Next |
| Accelerated rendering | CPU framebuffer | Content/performance ceiling | Build seam/backend selectively | Next/Later |
| Production database | Embedded SQLite schema | Upgrade, concurrency, HA, recovery risk | Integrate PostgreSQL; build adapter/migrations | Next |
| RBAC/audit/secrets | Static admin and project secrets | Unsafe external operators and weak accountability | Build domain; integrate identity/secrets | Next |
| Observability/SLO | in-process request counters | Cannot diagnose or operate reliably | Build instrumentation; integrate backend | Next |
| Economy correctness | inventory grant/consume | Monetization would be unsafe | Build only with reference game | Later |
| LiveOps experimentation | config/events/analytics basics | Cannot measure causal changes | Build exposure/segment contracts | Later |
| Social/trust | lobby presence only | No durable community safety | Build slice; integrate moderation tools | Later |
| Dedicated authority | in-memory realtime hub | Competitive realtime games cannot trust clients | Integrate provider via seam | Conditional |
| Public catalog | local asset collections | No discovery; enormous trust/policy burden | Defer | Conditional |
| Payments/payouts | absent | Legal, fraud, tax, support burden | Integrate only after gate | Conditional |

## Build-versus-integrate policy

### Build because it teaches or differentiates

- deterministic engine/simulation cores;
- resource IDs, simple versioned formats, and migrations;
- project manifest and golden-path CLI;
- Studio workflow and exact packaged preview;
- API domain contract and compatibility tests;
- local Game BaaS behavior and reference adapters;
- reference games, guidebook, exercises, and failure drills.

### Integrate because it is commodity or operationally specialized

- TLS certificates, edge proxy, DNS, and CDN;
- PostgreSQL engine, object storage, secrets store, container runtime;
- metrics/log/trace storage and alert delivery;
- email/SMS, payments, store receipts, platform identities;
- voice, anti-cheat, malware scanning, content moderation services;
- Kubernetes/GameLift/Edgegap/Agones-level game-server orchestration;
- external stores and public distribution.

Integration still requires owned adapters, tests, failure behavior, cost budgets, and
exit plans. “Use a vendor” is not an architecture.

## Structural risks

### Breadth before product

The repository can add another core library or BaaS controller quickly. Each addition
looks like progress but increases build time, tests, docs, security surface, and user
choice. Require a golden-path consumer and retirement plan for every new capability.

### Documentation drift

Ninety chapters plus specs and plans can contradict runtime behavior.
Introduce supported-version metadata, journey indexes, and automated local link checks.
Keep one canonical document per decision type.

### Trust-domain collapse

Public client keys, player tokens, project secrets, and platform admin secrets are
different authorities. The prototype models some separation, but external operation
needs explicit identities, scopes, expiry, rotation, audit, and incident recovery.

### Production label inflation

Passing integration tests proves behavior, not availability, recovery, security, or
operability. Strategy and README language must say “functional” or “reference” until
production acceptance is evidenced.

### Marketplace scope trap

Asset upload/list/download is a registry. A marketplace adds discovery, ranking,
ownership, licensing, moderation, malware, policy, payments, tax, refunds, fraud,
support, and appeals. Treat each as a gated product phase.

### Rendering scope trap

Modern GPU rendering can consume the entire roadmap. Add the minimum render-resource
abstraction and one accelerated path serving chosen games; retain the CPU renderer as
reference and test oracle.

### Single-maintainer sustainability

Every supported platform and service creates recurring work. Limit the supported
matrix, automate compatibility, publish a deprecation policy, and prefer fewer deep
reference games over a growing demo list.

## Ranked top-ten priorities

1. **Canonical project manifest and golden-path CLI** — creates the contract every
   UI, test, build, and release action can share.
2. **Resource identity, schema versioning, and packaged dependency graph** — connects
   Studio, runtime, Web build, and registry.
3. **Reference-game selection and end-to-end acceptance** — turns current games into
   product evidence.
4. **OpenAPI contract plus SDK conformance tests** — prevents backend/client drift and
   unlocks TypeScript.
5. **Clean-checkout multi-platform CI and release provenance** — makes support claims
   reproducible.
6. **Immutable Web preview, promotion, and rollback** — completes the shortest user
   feedback loop.
7. **Unified Studio shell with undo/recovery/validation** — converts Labs into an
   authoring product.
8. **Production BaaS foundations** — migrations, PostgreSQL path, roles, secrets,
   audit, backup/restore, trace context, and runbooks.
9. **Measured LiveOps loop** — segments, scheduled config, experiment exposure, data
   quality, and reversible changes.
10. **Selective runtime depth** — input actions, 2D animation, audio, materials, and
    an accelerated render seam only as demanded by reference games.

## What not to prioritize now

- a new standalone game genre without platform-journey coverage;
- more BaaS endpoints without contract and operating foundations;
- microservice decomposition of the modular monolith;
- a custom global game-server scheduler;
- broad 3D importer/shader parity;
- SDKs for every engine before TypeScript and one evidence-selected adapter;
- public marketplace, virtual currency sale, creator payouts, or sponsored discovery;
- AI-generated asset publishing without provenance and policy.

## Baseline conclusion

The project already proves that its architectural ideas work. The next challenge is
to prove that a user can repeatedly travel through them. Maturity should now be earned
by connection, reproducibility, failure recovery, and external completion—not by the
number of finished subsystem rows.
