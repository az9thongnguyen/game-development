# Game Platform Strategy 2026–2029 — Design Spec

**Date:** 2026-07-11
**Status:** Approved for documentation
**Scope:** Product strategy, target architecture, capability priorities, roadmap,
metrics, and competitive monitoring. Documentation only; no runtime or backend code.

## 1. Decision

Evolve the repository from a collection of strong learning milestones into a
coherent **transparent, self-hostable game creation platform** for solo developers
and small indie teams.

The platform promise is:

> Learn, create, ship, and operate a small game from one inspectable C++/WebAssembly
> stack—without hiding the engine, content pipeline, or backend behind a black box.

The project will not try to beat Unity, Unreal, or Godot on general-purpose engine
breadth, nor PlayFab, Unity Gaming Services, or Epic Online Services on managed-cloud
scale. It will differentiate through an end-to-end workflow whose implementation is
small enough to understand, modify, self-host, and teach.

## 2. Why this direction

### 2.1 Market signals

- Newzoo estimated the 2025 games market at **USD 188.8 billion** and **3.6 billion
  players**, with the player base approaching 3.9 billion by 2028. Its 2025 report
  explicitly highlights post-launch content and player retention as strategic themes.
  Source: [Newzoo Global Games Market Report 2025](https://newzoo.com/resources/trend-reports/newzoo-global-games-market-report-2025).
- GDC's 2026 survey of more than 2,300 industry professionals found PC remains a
  leading target, Steam Deck interest is material, and Unreal/Unity remain dominant.
  This makes a head-on general-engine competition an unattractive use of a small
  project's resources. Source: [GDC 2026 State of the Game Industry](https://gdconf.com/article/gdc-2026-state-of-the-game-industry-reveals-impact-of-layoffs-generative-ai-and-more/).
- Game backend competitors have made authentication, cloud save, leaderboards,
  remote configuration, and basic matchmaking table stakes. Their differentiation
  has moved toward authoritative multiplayer, economy, social systems, LiveOps,
  experimentation, observability, and engine-native workflows. Sources:
  [Unity Gaming Services](https://unity.com/products/gaming-services/pricing),
  [PlayFab](https://learn.microsoft.com/en-us/xbox/playfab/get-started/),
  [Epic Online Services](https://onlineservices.epicgames.com/licensing),
  [Nakama](https://heroiclabs.com/nakama/), and
  [Pragma](https://pragma.gg/docs/0.5.0/introduction/features).
- Creator platforms show that authoring, publishing, discovery, analytics, community,
  and monetization reinforce one another. Roblox reported more than USD 1 billion in
  creator earnings in the twelve months ending June 2025; Epic reported 260,000 live
  creator-made Fortnite islands and 11.2 billion hours played since UEFN launched.
  These figures validate the workflow, not a decision to build a public marketplace
  immediately. Sources: [Roblox RDC 2025](https://ir.roblox.com/news/news-details/2025/Roblox-Unveils-AI-Monetization-and-Performance-Innovations-for-Creators/)
  and [Fortnite creator ecosystem](https://www.fortnite.com/news/fortnite-developers-will-soon-be-able-to-sell-in-game-items?lang=en-US).
- Web delivery has improved: WebGPU is supported across the major browser families,
  while WebAssembly has standardized SIMD and broad thread support. The existing
  C++/WASM architecture is therefore a durable advantage worth productizing.
  Sources: [WebGPU browser support](https://web.dev/blog/webgpu-supported-major-browsers)
  and [WebAssembly feature status](https://webassembly.org/features/).

### 2.2 Repository signals

The repository already contains more than an engine prototype:

- a hand-written C++20 engine with software 2D/3D rendering, math, input, audio,
  assets, ECS, jobs, allocators, physics, UI, font rendering, and hot reload;
- native and WebAssembly targets sharing game and engine logic;
- multiple games and sandboxes that exercise different engine layers;
- Mini Studio authoring tools and engine-native asset formats;
- a Game BaaS with authentication, cloud saves, inventory, leaderboards, remote
  config, analytics, live events, WebSocket lobbies/matchmaking, replay storage,
  rate limiting, metrics, dashboard, and C++ SDK;
- a long-form guidebook tied directly to the implementation.

The missing value is not another isolated subsystem. The missing value is a reliable
path from **new project → create → test → publish → operate → learn from results**.

## 3. Target users

### Primary: the owner and serious learners

Developers who want to understand engine and online-game architecture by reading,
changing, and running a complete implementation. They value explanation,
determinism, and inspectability above one-click convenience.

### Secondary: solo and 2–5 person indie teams

Teams building small 2D, 2.5D, retro 3D, simulation, tactics, or lightweight online
games. They need a fast browser preview, a self-hostable backend, reproducible builds,
and enough LiveOps to validate a game without assembling a large cloud stack.

### Deferred: public creator ecosystem

External creators publishing to a shared catalog. This becomes a target only after
the platform supports external onboarding and at least three reference games have
proven the workflow. Public discovery, commerce, payouts, moderation, fraud, tax,
and policy are separate product domains and must not be smuggled into an asset upload
feature.

## 4. Product principles

1. **One golden path before more features.** Every major investment must improve the
   create-to-operate loop for at least one reference game.
2. **Glass box, not black box.** Core behavior remains readable, documented, and
   replaceable. The guidebook explains why the design exists, not only how to call it.
3. **Reference implementation plus production seam.** The CPU renderer, SQLite local
   mode, and hand-written formats remain educational references; production backends
   are added behind explicit interfaces rather than replacing the reference path.
4. **Local-first and self-hostable.** A developer can create and test a project
   offline. Cloud services add collaboration and distribution, not basic editability.
5. **Web preview is a first-class release target.** A shareable browser build is the
   shortest path from creation to feedback.
6. **Build vs. integrate is explicit.** Build mechanisms that teach or differentiate;
   integrate commodity infrastructure such as TLS termination, object storage,
   container orchestration, voice, anti-cheat, payments, and transactional email.
7. **Production readiness is a measurable claim.** A feature is not production-ready
   until deployment, migration, backup, observability, security, and rollback are
   documented and verified.
8. **AI is assistive and accountable.** AI may help with research, code, tests,
   recipes, and documentation, but generated assets require provenance and explicit
   user action. AI is not the product position. GDC 2026 found 52% of respondents
   viewed generative AI's industry impact negatively, so trust matters.

## 5. Strategic alternatives considered

### A. General-purpose engine and editor

Strength: maximizes engine depth and learning value.
Weakness: enters the broadest competitive field and creates endless parity work in
rendering, animation, importers, platform support, editor UX, and debugging.
Decision: retain engine depth as one platform pillar, but do not make feature-count
parity the strategy.

### B. Standalone Game BaaS

Strength: the existing backend is a strong foundation and the category is easier to
explain.
Weakness: PlayFab, UGS, EOS, Nakama, Beamable, and Pragma already cover the standard
capability set. A C++-only client and custom API are barriers to adoption.
Decision: productionize BaaS as the operating layer of the integrated platform, not
as an isolated clone of established services.

### C. Focused full-stack platform — selected

Strength: uniquely combines a teachable engine, native authoring, web delivery,
self-hosted online services, and guidebook. Each reference game can validate the
whole system.
Weakness: cross-layer work can fragment unless milestones are vertical and gated.
Decision: organize the roadmap around complete user journeys and explicit stage
gates rather than independent subsystem backlogs.

## 6. Target product loop

The canonical loop must eventually be achievable without editing source files by
hand:

1. Create a project from a versioned template.
2. Open it in a platform hub and launch the relevant authoring tools.
3. Create or import versioned engine-native assets.
4. Assemble a scene from resources, prefabs, and behaviors.
5. Run deterministic local tests and an interactive preview.
6. Build native and browser artifacts using named build profiles.
7. Publish an immutable preview release and share its URL.
8. Promote a tested release to production or roll back to a previous release.
9. Configure an event or experiment without rebuilding the client.
10. Observe health, errors, usage, retention, and cost.
11. Feed the result into the next content or gameplay iteration.

Each roadmap phase must shorten, stabilize, or broaden this loop.

## 7. Capability model

### 7.1 Platform spine — highest priority

- canonical `game.project` manifest with identity, scenes, assets, backend endpoint,
  and build profiles;
- a platform hub that lists projects, launches tools, runs tests, builds, publishes,
  and links to operations;
- stable resource IDs, schema versions, dependency metadata, and migrations;
- one CLI contract underlying GUI actions for reproducibility and CI;
- immutable build and asset artifacts with development, preview, and production
  channels;
- generated API contract and SDK compatibility tests.

### 7.2 Engine runtime

- keep the software renderer as a testable reference;
- introduce a `RenderDevice` boundary before adding an accelerated backend;
- use OpenGL ES 3/WebGL2 as the pragmatic first accelerated path already anticipated
  by `requirements.md`; evaluate WebGPU as a later backend after the resource and
  material abstractions are stable;
- add a resource/scene/prefab model, material system, 2D animation, particles,
  audio mixer/spatialization, input actions, gamepad/touch, profiling, and build
  budgets in that order of workflow value;
- preserve deterministic, headless-testable simulation cores.

### 7.3 Studio and content pipeline

- unify Texture Lab, Map Lab, sandbox, and future composers under a shared Studio
  shell rather than accumulating unrelated command-line scenes;
- add undo/redo, dirty-state tracking, autosave/recovery, asset browser, metadata,
  thumbnails, validation, dependency inspection, and import/export;
- treat collections as versioned resources, not directory scans;
- add one-click preview using the exact packaged assets that will be published;
- keep authoring native-first while adding durable browser persistence only when a
  real web-authoring workflow is selected.

### 7.4 Game services and LiveOps

- formal OpenAPI contract and TypeScript SDK before more service breadth;
- production persistence path with schema migrations, PostgreSQL, backups, and
  restore drills while retaining SQLite for local development;
- project roles, short-lived credentials, secret rotation, audit logs, and separate
  operator/server/client trust domains;
- economy catalog, currencies, atomic transactions, idempotency, and receipt
  validation before monetization features;
- player segments, scheduled config, experiments, exposure events, and result
  analysis before advanced personalization;
- friends, presence, chat, reports, and sanctions as a coherent social/trust slice;
- authoritative session hosting only after a reference game requires it. Integrate
  GameLift, Edgegap, Agones, or another orchestrator instead of building a cluster
  scheduler from scratch.

### 7.5 Delivery and operations

- reproducible macOS, Linux, Windows, and Web builds in CI;
- containerized BaaS and documented local/production configuration;
- release metadata, checksums, promotion history, preview URLs, health checks, and
  instant rollback;
- structured logs, metrics, traces, correlation IDs, dashboards, SLOs, alerts, and
  capacity/cost budgets;
- privacy export/deletion, retention policies, backup verification, incident runbook,
  and dependency/license inventory before external beta.

### 7.6 Distribution and ecosystem

- private project catalog first;
- shareable browser previews second;
- curated showcase after three reference games;
- public publishing only with ownership, moderation, reporting, rating, takedown,
  versioning, malware scanning, and policy operations designed together;
- commerce and creator payouts remain out of scope until discovery and retention
  demonstrate value without them.

## 8. Roadmap horizons

### Horizon 0 — 0–3 months: make it one product

Deliver the project manifest, CLI contract, platform hub information architecture,
API contract, CI matrix, resource identity/version rules, and a complete local
golden path for one reference game.

Exit gate: a clean checkout can create, build, test, and run the reference game from
documented commands without source edits or hidden machine state.

### Horizon 1 — 3–6 months: create-to-publish

Deliver Studio shell v2, undo/redo, asset registry, packaged preview, immutable Web
release, preview URL, promotion, and rollback.

Exit gate: a new map/texture/content change can be authored, packaged, published,
shared, and rolled back in under 15 minutes.

### Horizon 2 — 6–12 months: operate a real small game

Deliver production persistence, deployment profiles, RBAC/audit, backups, full
telemetry, LiveOps segmentation/experiments, economy foundations, and a second
reference game that exercises online operations.

Exit gate: the platform survives a documented failure drill, measures a release,
and runs a reversible LiveOps change without client redeployment.

### Horizon 3 — 12–24 months: external developer beta

Deliver onboarding, templates, TypeScript SDK, one mainstream engine adapter,
containerized self-hosting, versioned APIs, migration policy, support/runbooks, and
three polished reference games.

Exit gate: three external teams complete the golden path without repository-author
intervention, and at least two publish a second iteration.

### Horizon 4 — 24–36 months: curated creator ecosystem

Deliver a curated catalog and community/discovery features only if external beta
shows repeat creation, publishing, and player retention. Public uploads and commerce
remain separate gated initiatives.

Exit gate: evidence of repeat creators, repeat players, acceptable moderation load,
and sustainable hosting cost.

## 9. Priority method

Every proposed initiative receives a score from 0–5 on:

- golden-path impact;
- learning/differentiation value;
- number of existing capabilities it connects;
- user evidence;
- operational risk reduction;
- maintenance cost, scored negatively;
- external dependency risk, scored negatively.

Work that connects three existing layers generally outranks a deep feature in one
layer. Security, data-loss prevention, and release rollback can override the score.

## 10. Success metrics and stage gates

The strategy documentation will define measurable indicators in five groups:

- **Activation:** time to first successful build and browser preview; golden-path
  completion rate.
- **Creation:** time from project creation to first authored asset and first playable
  scene; build and asset validation failure rate.
- **Publishing:** publish success rate, median preview build time, rollback time, and
  artifact reproducibility.
- **Operation:** API availability, error rate, p95 latency, restore-point age, restore
  success, experiment correctness, and cost per monthly active user.
- **Learning/ecosystem:** guidebook completion, exercise completion, repeat project
  creation, external developer retention, and reference-game coverage.

Feature count, registered users without activation, and raw asset uploads are not
success metrics.

## 11. Documentation package

The approved strategy will be decomposed into linked documents so each audience can
read only what it needs:

1. `docs/strategy/README.md` — navigation, thesis, assumptions, and update policy.
2. `docs/strategy/01-market-and-positioning.md` — market signals, landscape, three
   alternatives, chosen position, opportunities, threats, and source register.
3. `docs/strategy/02-current-state-and-gap-analysis.md` — repository evidence,
   capability maturity, risks, build-vs-integrate decisions, and priority gaps.
4. `docs/strategy/03-target-platform-architecture.md` — target layers, product loop,
   control/data/content flows, boundaries, trust zones, and architectural decisions.
5. `docs/strategy/04-roadmap-2026-2029.md` — horizons, vertical initiatives,
   dependencies, exit criteria, explicit non-goals, and trigger-based later bets.
6. `docs/strategy/05-product-metrics-and-stage-gates.md` — north-star behavior,
   metric definitions, instrumentation needs, scorecard, and decision gates.
7. `docs/strategy/06-competitive-watchlist.md` — dated capability comparison,
   competitor profiles, monitoring cadence, and signals that should change strategy.
8. `README.md` and `requirements.md` — concise links and a clear distinction between
   the original learning vision, delivered milestones, and the new product strategy.

## 12. Source and writing policy

- Date-sensitive external claims carry a direct link and an "as of" date.
- Prefer first-party product documentation, standards bodies, investor filings, and
  original surveys. Vendor claims are labeled as vendor claims.
- Competitive ratings compare buyer-relevant capability areas, not raw feature count.
- Clearly distinguish observed repository facts, sourced market facts, and strategic
  inference.
- Use `Current`, `Next`, `Later`, and `Conditional`; avoid ambiguous "future" buckets.
- No `TBD`, `TODO`, invented adoption numbers, or claims of production readiness.
- Link canonical material rather than duplicating detailed subsystem documentation.

## 13. Documentation acceptance criteria

- All eight documentation changes in section 11 exist and link to one another.
- The market brief includes dated sources and an honest comparison where competitors
  lead the current project.
- The gap analysis maps every roadmap initiative to observed repository evidence.
- The target architecture preserves the existing platform, asset, tick, SDK transport,
  and backend process boundaries.
- The roadmap contains dependencies and exit gates, not calendar promises alone.
- Metrics have formulas, owners by role, collection points, and decision thresholds.
- The watchlist has an update cadence and no comparison that makes this project win
  every category.
- README and requirements continue to describe implemented functionality accurately.
- Only Markdown documentation changes are committed on the strategy branch.

## 14. Risks and mitigations

### Scope expands back into a general engine

Mitigation: require each engine investment to unlock a named reference-game journey;
keep the accelerated renderer behind a seam and postpone broad importer parity.

### Backend breadth hides production weaknesses

Mitigation: prioritize contracts, migrations, RBAC, backups, telemetry, deployment,
and rollback before adding more standalone services.

### A public marketplace is mistaken for file hosting

Mitigation: gate public publishing behind external-beta evidence and design discovery,
trust, moderation, policy, and economics as their own product phase.

### Learning-first and product-first goals conflict

Mitigation: maintain two modes behind the same interfaces: readable local reference
implementations and documented production adapters. Do not make the learning path
depend on paid cloud services.

### AI novelty distorts priorities

Mitigation: measure whether an AI feature shortens a verified workflow; require
provenance, deterministic export where possible, and a non-AI path.

## 15. Non-goals of this documentation branch

- No source code, schema, API, deployment, UI, or build-system implementation.
- No rename of the repository or public product branding.
- No public marketplace, payments, creator payout, or moderation implementation plan.
- No commitment to Kubernetes, a cloud provider, WebGPU, or a third-party BaaS before
  an architecture decision record and reference-game need.
- No replacement of the existing hand-written engine or guidebook.
