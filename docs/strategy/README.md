# Game Platform Strategy 2026–2029

This directory defines how the repository grows from a hand-written engine and a
set of successful learning milestones into one coherent game creation platform.
It does not replace [`requirements.md`](../../requirements.md), the guidebook, or
feature design specs. It turns them into a longer product direction with explicit
priorities, boundaries, evidence, and stop conditions.

## Strategy in one paragraph

Build a **transparent, self-hostable game creation platform** for serious learners,
solo developers, and 2–5 person indie teams. The platform should let one developer
move from project creation to content authoring, deterministic testing, native/Web
builds, preview publishing, Game BaaS integration, LiveOps, and operational feedback
without assembling unrelated products. Its advantage is not feature-count parity
with Unity, Unreal, Godot, or PlayFab. Its advantage is that the complete path is
small enough to inspect, learn, modify, and run locally.

> **Product promise:** Learn, create, ship, and operate a small game from one
> inspectable C++/WebAssembly stack.

## Adaptation record — 2026-07-11 (adopted into the repo)

This package was authored in a separate strategy worktree pinned at commit `ad7349b`.
It was reviewed, fact-checked against primary sources, and brought into the live
repository at HEAD `9cedf29` (90 guidebook chapters). Three changes were made so the
strategy fits the project as it actually is:

1. **Re-baselined to current code.** The gap analysis now records the twelve
   milestones added since `ad7349b` — including a BaaS asset registry and a
   test-runner that *partially* fill gaps this package first listed as absent, and a
   run of engine-depth features (particles, tween, 2D lighting, audio mixer, sprite
   animation) that are the exact "isolated runtime depth ahead of a golden-path
   consumer" the roadmap says to now pause. See
   [02 §10](02-current-state-and-gap-analysis.md).
2. **Adopted execution posture: _blend_ (owner's decision).** The category and the
   dependency spine are kept as the north star, but the near-term cadence is *one*
   reference game and *one thin golden path first*, interleaving a golden-path slice
   with a hand-written runtime/learning slice each cycle to sustain a solo maintainer's
   momentum. Full production hardening, a three-game portfolio, external beta, and any
   public catalog remain genuinely **conditional/later**, not a committed multi-year
   march. See the posture callout in [04](04-roadmap-2026-2029.md).
3. **Two dated facts softened after 2026 verification.** Newzoo later revised its 2025
   estimate upward to ~USD 197B (the original figure is retained as the dated
   baseline); the WebGL2-before-WebGPU ordering is kept *for a hand-written engine* but
   noted as no longer the industry default; and "Godot + Nakama is the single biggest
   threat" is corrected to "one credible open stack among several, whose frictionless
   local-to-live onboarding already exists today." See [03](03-target-platform-architecture.md)
   and [06](06-competitive-watchlist.md).

Everything else in this package is preserved as originally written. All external facts
were verified against primary sources on 2026-07-11.

## Who this is for

| Audience | Need | Product response |
|---|---|---|
| Owner and serious learner | Understand how a full game platform works | Readable implementations, guidebook, deterministic tests, explicit seams |
| Solo developer | Turn an idea into a shareable build quickly | Project templates, Studio, one golden path, browser preview |
| 2–5 person indie team | Operate a small online game without a large platform team | Self-hosted BaaS, release channels, LiveOps, telemetry, rollback |
| External creator | Adopt the platform without author assistance | Conditional 12–24 month beta after the internal workflow is proven |
| Public marketplace participant | Publish/discover/monetize content | Deferred until trust, moderation, economics, and retention gates pass |

## Current strategic decisions

1. Choose a focused full-stack platform, not a general engine feature race or a
   standalone BaaS clone.
2. Make **new project → create → test → publish → operate → learn** the canonical
   product loop.
3. Keep the software renderer and SQLite backend as reference implementations;
   add production adapters behind stable boundaries.
4. Make a shareable WebAssembly preview the shortest feedback path.
5. Productionize contracts, releases, security, backups, and observability before
   adding more isolated backend features.
6. Build a private asset/release registry before considering a public catalog.
7. Use AI as an opt-in assistant with provenance, never as the core positioning.
8. **Execute as a _blend_ (2026-07-11):** connect the existing pile of subsystems
   through one thin golden path around a single reference game before adding breadth,
   and interleave that plumbing with hand-written runtime/learning slices so the solo
   maintainer keeps momentum. The multi-horizon product build-out remains the map, but
   its later, ops-heavy, external-user stages stay conditional on evidence, not
   scheduled.

## Document map

| Document | Decision it owns |
|---|---|
| [Market and positioning](01-market-and-positioning.md) | Who we serve, what category we enter, where we differentiate |
| [Current state and gap analysis](02-current-state-and-gap-analysis.md) | What exists, how mature it is, what is missing, what to build or integrate |
| [Target platform architecture](03-target-platform-architecture.md) | Target layers, flows, seams, trust boundaries, build-vs-integrate rules |
| [Roadmap 2026–2029](04-roadmap-2026-2029.md) | Sequence, dependencies, horizon outcomes, non-goals, exit gates |
| [Product metrics and stage gates](05-product-metrics-and-stage-gates.md) | How progress is measured and when later bets are permitted |
| [Competitive watchlist](06-competitive-watchlist.md) | Honest comparison, threats, monitoring cadence, strategy-change signals |
| [Approved design spec](../superpowers/specs/2026-07-11-game-platform-strategy-design.md) | Source decision and documentation acceptance criteria |
| [Execution plan](../superpowers/plans/2026-07-11-game-platform-strategy-docs.md) | How this documentation package was produced and verified |

## How to use the strategy

Before proposing a milestone, answer these questions:

1. Which step of the canonical product loop becomes faster or safer?
2. Which named user experiences the benefit?
3. Which reference game proves the benefit end to end?
4. Is the mechanism differentiating/educational enough to build, or commodity
   infrastructure that should be integrated?
5. What measurable exit condition prevents the milestone from remaining “almost
   complete”?
6. What is explicitly not included?

An initiative that cannot answer these questions stays in the idea backlog. Security,
data-loss prevention, and release rollback may override normal prioritization.

## Evidence labels

Strategy documents use four evidence types:

- **Repository observation:** verified from current files, build targets, tests, or
  behavior in this repository.
- **External fact:** supported by a linked primary source or original survey and
  dated when it may change.
- **Vendor claim:** a capability, scale, price, or outcome stated by the vendor; it
  is useful for positioning but not treated as independently verified performance.
- **Strategic inference:** a recommendation derived from observations and external
  facts. Inferences are labeled and can be changed without rewriting history.

## Review and update policy

- **Monthly:** verify competitor pricing, service shutdowns, material license
  changes, and browser/platform compatibility.
- **Quarterly:** review roadmap gates, reference-game evidence, capability maturity,
  and the competitive response matrix.
- **At every horizon exit:** update the current-state assessment before authorizing
  the next horizon.
- **Annually:** revalidate target users, category positioning, market assumptions,
  the 24–36 month conditional bets, and the source register.

Every material update records the evidence date. A changed vendor price does not by
itself rewrite strategy; a changed user need, platform constraint, or failed stage
gate does.

## Glossary

- **BaaS:** Game Backend-as-a-Service: game-facing identity, data, LiveOps, social,
  and multiplayer services outside the engine process.
- **Golden path:** the documented, supported route from a clean checkout to a
  published and observable reference game.
- **LiveOps:** changing and measuring a live game through configuration, content,
  events, segments, and experiments without rebuilding the client.
- **Platform hub:** the future product shell that discovers projects and invokes
  create, author, validate, build, publish, and operate workflows.
- **Reference game:** a maintained game used as an integration contract and proof of
  a complete user journey, not merely a subsystem demo.
- **Self-hosted:** runnable in infrastructure controlled by the developer, with
  documented configuration, upgrades, backup, and restore.
- **Stage gate:** measurable evidence required before starting a larger or riskier
  product phase.
- **Studio:** the content-authoring environment that unifies current and future Labs,
  scene composition, asset inspection, and preview.
- **Transparent platform:** an end-to-end stack whose important behavior and data
  formats can be inspected, tested, and replaced through documented boundaries.
