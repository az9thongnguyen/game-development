# Market and Positioning

**Evidence date:** 2026-07-11
**Decision horizon:** 2026–2029
**Primary decision:** Where this repository can create distinctive user value without
entering an unwinnable feature-count race.

## Executive conclusion

The repository should occupy the space between an educational engine project and a
commercial black-box game stack:

> **For serious learners and small indie teams who need to understand and own their
> stack, this is a transparent, self-hostable game creation platform that connects
> engine, authoring, Web delivery, online services, and operations. Unlike a general
> engine plus several SaaS products, it provides one inspectable golden path from
> source and content to a published, observable game.**

This is a strategic inference from the market and repository evidence below. It is
not a claim that the current project is externally adoptable or production-ready.

## Market signals

### A large market does not imply a broad product

Newzoo estimated **USD 188.8 billion** in game revenue and **3.6 billion players** in
2025, with a forecast of USD 206.5 billion and nearly 3.9 billion players by 2028.
(Newzoo later revised its full-year 2025 estimate upward to ~USD 197 billion; the
original-report figures are retained here as the dated 2026-07-11 baseline.)
Its report highlights post-launch content and retention as strategic themes. The
relevant implication is that value extends beyond rendering and initial development;
tools for iteration, publishing, content, and operations are part of the product.

Source: [Newzoo Global Games Market Report 2025](https://newzoo.com/resources/trend-reports/newzoo-global-games-market-report-2025).

### PC, handheld PC, and Web fit the existing architecture

GDC's 2026 State of the Game Industry survey gathered responses from more than 2,300
professionals. Among its findings:

- 73% of surveyed executives placed PC among their top three next-generation target
  platforms;
- 28% of developers were making or optimizing games for Steam Deck and 40% expressed
  interest;
- Unreal was the primary engine for 42% of respondents and Unity for 30%; Godot had
  gained use among newer indie developers.

Survey populations and self-reporting limit generalization, but the direction is
clear: PC remains important, handheld compatibility matters, and established engines
own general-purpose breadth. This project should support efficient PC/Web delivery
while differentiating on transparency and integration.

Source: [GDC 2026 State of the Game Industry](https://gdconf.com/article/gdc-2026-state-of-the-game-industry-reveals-impact-of-layoffs-generative-ai-and-more/).

### Backend basics are already commodities

As of the evidence date:

- Unity Gaming Services sells authentication, cloud save, economy, cloud code,
  leaderboards, analytics, remote configuration, relay, lobby, friends, and voice
  through free tiers and usage pricing. Source: [UGS pricing](https://unity.com/products/gaming-services/pricing).
- Microsoft PlayFab groups identity, LiveOps, economy/UGC, multiplayer, community,
  player progression, and game data under one operating platform. Its 2026 Foundation
  Mode for eligible Xbox developers includes core identity, saves, catalog, inventory,
  statistics, and related services. Sources: [PlayFab fundamentals](https://learn.microsoft.com/en-us/xbox/playfab/get-started/)
  and [Foundation Mode](https://learn.microsoft.com/en-us/xbox/playfab/get-started/mode-overview).
- Epic Online Services offers most account, crossplay, lobby, session, stats,
  achievements, player-data, reporting, sanctions, voice, and anti-cheat services
  without usage or hosting fees under its standard game licensing; some support and
  advanced anti-cheat arrangements are enterprise offerings. Sources:
  [EOS licensing](https://onlineservices.epicgames.com/licensing) and
  [EOS service agreement](https://onlineservices.epicgames.com/services/terms/agreements?lang=en-US).
- Nakama positions open-source ownership, authoritative and relayed multiplayer,
  matchmaking, competitive systems, social features, and server customization as its
  core. Source: [Nakama product page](https://heroiclabs.com/nakama/).
- Pragma describes a platform covering cross-platform accounts, social, matchmaking,
  game-server allocation, content data, monetization, telemetry, and operations.
  Source: [Pragma features](https://pragma.gg/docs/0.5.0/introduction/features).

The current BaaS has meaningful overlap with these products, but overlap is not a
position. Competing on a checklist would require SDK reach, service reliability,
global infrastructure, integrations, support, compliance, and years of operations.

### Creator platforms compete on a loop, not an editor alone

Roblox reported that creators earned more than USD 1 billion through DevEx during the
twelve months ending June 30, 2025. It combines creation, publishing, discovery,
social presence, analytics, economy, and payouts. Roblox also disclosed that more
than 29,000 creators participated in DevEx and the median participant received USD
1,440 during that period—a reminder that ecosystem opportunity is highly uneven.

Source: [Roblox RDC 2025 investor release](https://ir.roblox.com/news/news-details/2025/Roblox-Unveils-AI-Monetization-and-Performance-Innovations-for-Creators/).

Epic reported in September 2025 that UEFN/Creative had 260,000 live creator-made
islands, more than 11.2 billion hours played, and USD 722 million paid to creators
since UEFN launched. The same announcement connects publishing with discovery,
acquisition, retention, analytics, in-island transactions, and community tools.

Source: [Fortnite creator ecosystem](https://www.fortnite.com/news/fortnite-developers-will-soon-be-able-to-sell-in-game-items?lang=en-US).

These are vendor-reported figures, not a forecast for this project. They validate the
importance of a connected creation loop and simultaneously show why public discovery,
fraud, moderation, and payouts must not be treated as a simple file registry.

### Browser delivery is becoming more capable

WebGPU reached support across the major browser families by late 2025, although the
"differences remain" caveat is load-bearing: as of mid-2026 Firefox still does not ship
WebGPU on Linux, Android, or Intel Macs, and Safari requires the OS-26 generation, so
roughly 15–30% of browsers still need a fallback. WebAssembly implementations broadly
support fixed-width SIMD and threads (threads require cross-origin isolation), while
proposals continue to improve integration and componentization. The strategic
implication is to preserve WebAssembly and plan a GPU abstraction—not to switch
immediately before the resource and material model exists. (Note: by 2026 the
framework default for *new* projects has shifted to WebGPU-with-WebGL2-fallback; this
package still recommends WebGL2 as the first backend for a *hand-written* engine, but
treats WebGPU as a co-equal option rather than a distant one — see
[03](03-target-platform-architecture.md).)

Sources: [WebGPU support across major browsers](https://web.dev/blog/webgpu-supported-major-browsers),
[W3C WebGPU publication history](https://www.w3.org/standards/history/webgpu/), and
[WebAssembly feature status](https://webassembly.org/features/).

### AI is useful but a weak trust position

The same GDC 2026 survey reported 36% professional use of generative AI, often for
research, daily tasks, code assistance, and prototyping. It also found 52% believed
generative AI had a negative industry impact, rising from 30% in the prior survey.
Therefore AI assistance may shorten workflows, but an “AI game platform” position
would create trust and differentiation risk. Deterministic outputs, provenance,
human approval, and a complete non-AI path are product requirements.

Source: [GDC 2026 State of the Game Industry](https://gdconf.com/article/gdc-2026-state-of-the-game-industry-reveals-impact-of-layoffs-generative-ai-and-more/).

## Competitive landscape by job

| Product group | Job it performs well | Representative products | Implication |
|---|---|---|---|
| General engines | Build many game types and export broadly | Unreal, Unity, Godot, O3DE | Do not pursue breadth parity; preserve a focused runtime and learning value |
| Managed game services | Avoid operating common backend capabilities | UGS, PlayFab, EOS | Reach contract/security/operability parity for the selected golden path, not service-count parity |
| Self-hosted/extensible backend | Own data and customize game logic | Nakama, Beamable private deployments | Self-hostability is necessary but not unique; integrated workflow and guidebook must differentiate |
| Dedicated-server platforms | Allocate and scale session servers | GameLift, Edgegap, Agones | Integrate behind a provider seam only when a reference game needs authority |
| Creator ecosystems | Create, publish, discover, engage, monetize | Roblox, UEFN/Fortnite | Learn from the loop; defer public ecosystem economics |
| Distribution | Put playable/downloadable games in front of players | itch.io, Steam | Export cleanly and link out before trying to replace distribution |

Itch.io already supports direct browser play for uploaded HTML/WASM packages, while
Steam provides mature distribution and operations tooling. The platform should emit
compatible artifacts rather than build a store prematurely. Sources:
[itch.io HTML5 upload documentation](https://itch.io/docs/creators/html5) and
[Steam Direct documentation](https://partner.steamgames.com/doc/gettingstarted/appfee).

## Buyer jobs and unmet combination

The primary users are not asking for “another renderer” or “another leaderboard.”
Their jobs are:

1. Understand what the platform does when something breaks.
2. Start a project without rebuilding infrastructure.
3. Author content and see the exact packaged result quickly.
4. Share a playable build without an installer.
5. Add online capabilities without blocking the game loop or losing Web portability.
6. Change a live game safely and measure the effect.
7. Own data and deploy locally before committing to cloud spend.
8. Learn from working implementation and exercises rather than API marketing.

Individual products solve most of these jobs. The less-common combination is a small,
inspectable implementation connecting all of them with one maintained reference path.

## Strategic alternatives

### Alternative A — general-purpose engine/editor

**Position:** a smaller open C++ engine competing with Godot/Unity/Unreal.
**Advantages:** aligned with existing technical depth; attractive learning content;
complete control.
**Costs:** perpetual parity work in importers, rendering, animation, scripting,
platform SDKs, editor UX, debugging, accessibility, and certification.
**Decision:** reject as the product strategy. Continue engine development only when
it unlocks a reference-game journey or explains a high-value concept.

### Alternative B — standalone open Game BaaS

**Position:** a self-hosted C++ alternative to PlayFab or Nakama.
**Advantages:** existing modular BaaS and SDK provide a real foundation; category is
understandable; self-hosting addresses ownership concerns.
**Costs:** current C++-only SDK, custom contract, SQLite-first persistence, limited
social/economy/operations, and no distribution advantage. Established competitors
are stronger in scale, ecosystem, integrations, and support.
**Decision:** reject as a standalone position. BaaS remains the operate layer of the
integrated platform.

### Alternative C — focused transparent full-stack platform

**Position:** an inspectable engine-to-LiveOps platform for learning and small games.
**Advantages:** uses every strong current capability; enables unique guidebook
coverage; a reference game can test all layers; self-hosted and Web-first workflows
are credible.
**Costs:** cross-layer work can fragment, and the team must resist adding isolated
features.
**Decision:** select, with vertical milestones and evidence gates.

## Positioning architecture

### Category

Transparent, self-hostable game creation platform.

### Primary differentiator

One inspectable path across runtime, content authoring, Web build, game services, and
operations, taught by the code and guidebook.

### Value proposition

Reduce the number of black boxes a developer must assemble while increasing the
amount they can understand, own, and change.

### Proof available today

- hand-written, tested 2D/3D software rendering and engine cores;
- multiple playable games and interactive tools;
- native/WebAssembly code sharing;
- engine-native authoring tools and assets;
- modular Game BaaS and non-blocking native/Web SDK transport seams;
- ninety implementation-linked guidebook chapters;
- all CTest suites passing in a clean build (the suite has grown past the 38 counted at
  this baseline as new engine/BaaS cores landed).

### Proof still required

- a project manifest and supported new-project workflow;
- unified Studio and resource model;
- reproducible CI across target platforms;
- packaged preview/publish/promotion/rollback;
- API contract and at least one additional-language SDK;
- production deployment, persistence, security, backup, traces, and runbooks;
- adoption by developers other than the repository owner.

## Honest strengths and weaknesses

| Area | Current strength | Current weakness |
|---|---|---|
| Learning | Deep, code-linked guidebook and hand-written mechanisms | Volume is high; no role-based learning path or measured completion |
| Runtime | Deterministic seams, software rendering, multiple games | Limited modern GPU/content/animation workflow and platform validation |
| Authoring | Texture Lab, sandbox, map work demonstrate asset production | Separate scenes, directory discovery, no shared project/resource lifecycle |
| Web | Same game logic builds to WASM | Manual scene selection, preview-only persistence, no release packaging workflow |
| BaaS | Broad functional slice and non-blocking SDK | Prototype operations, single SDK language, incomplete trust and production controls |
| Product | Unusual end-to-end ingredients | No single shell, manifest, golden path, installer, or external onboarding |
| Ecosystem | Self-contained and modifiable | No community, plugins, compatibility promise, governance, or support model |

## Opportunities

1. **Own the transparent full-stack learning position.** Godot is open and excellent,
   but this project can teach a deliberately smaller path including the backend and
   operations layer.
2. **Make browser preview the activation moment.** A URL is faster feedback than a
   store submission and fits the existing WASM design.
3. **Turn reference games into platform contracts.** Each game can prove authoring,
   packaging, BaaS, LiveOps, replay, and telemetry rather than adding another demo.
4. **Offer local-first BaaS plus production adapters.** Many competitors offer either
   managed convenience or self-hosted infrastructure; a paired teaching/production
   path is valuable if clearly supported.
5. **Use the guidebook as product UX.** Onboarding, architecture decisions, failure
   drills, and exercises can be part of the platform rather than documentation added
   after implementation.
6. **Publish interoperable artifacts.** Clean Web exports for itch.io/static hosts and
   native packages for external stores provide distribution without store-building.

## Threats

1. **Established products copy “transparent” messaging.** Godot/Nakama already offer
   real source ownership. The response must be a demonstrably simpler integrated
   workflow, not a slogan.
2. **Maintenance breadth exceeds one team.** Every new service or editor mode expands
   security, compatibility, documentation, and support. Vertical slices and explicit
   deletions are mandatory.
3. **Web performance expectations rise.** A CPU framebuffer alone will limit content.
   The response is a stable render/resource seam and measured accelerated backend,
   not abandoning the learning renderer.
4. **“Self-hosted” becomes an unsupported dump of source.** Deployment, upgrades,
   backup/restore, secrets, and observability must be part of the product claim.
5. **Public content introduces a different company.** Moderation, copyright, malware,
   child safety, fraud, payouts, and discovery can overwhelm engine progress.
6. **AI-generated content damages trust.** Provenance and opt-in workflows are needed
   before AI-created assets enter shared catalogs.

## Strategic implications

### Build or accelerate

- project manifest, resource identity/versioning, CLI contract, and platform hub;
- unified Studio workflow and packaged preview;
- OpenAPI plus TypeScript SDK and compatibility tests;
- immutable releases, environment promotion, rollback, and CI;
- production BaaS foundations before service breadth;
- three reference games that exercise distinct full-stack journeys.

### Reach parity where it affects trust

- migration discipline, idempotency, RBAC, credential separation, audit logs;
- backup/restore, metrics/logs/traces, SLOs, incident and rollback runbooks;
- input/accessibility basics, packaging correctness, API version policy;
- economy correctness and receipt validation before monetization.

### Integrate

- TLS certificates and edge proxy;
- PostgreSQL/object storage/container runtime;
- transactional email, platform identity, payments, voice, anti-cheat;
- dedicated game-server orchestration when justified;
- external stores and browser distribution.

### Deprioritize

- general engine parity;
- a custom cloud scheduler;
- public marketplace and creator payouts;
- AI-first branding;
- additional isolated BaaS endpoints without a reference-game consumer;
- advanced 3D features that do not serve a selected game or teaching path.

## Assumptions that would change this position

Reopen the strategy if any of the following becomes true:

- the primary user changes from learner/indie to mid-market or AAA studio;
- a funded team is formed specifically to commercialize backend infrastructure;
- external users consistently choose only the BaaS or only the engine;
- browser delivery ceases to matter for target users;
- three external teams cannot complete the golden path despite onboarding fixes;
- maintenance load prevents quarterly releases or timely security updates;
- evidence shows a narrower genre-specific tool creates substantially more repeat use.

Until such evidence exists, the selected position remains a focused full-stack
platform, with public ecosystem features conditional rather than promised.
