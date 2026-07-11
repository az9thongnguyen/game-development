# Game Platform Strategy Documentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Publish an evidence-backed 2026–2029 product strategy that turns the existing engine, Studio, WebAssembly build, Game BaaS, and guidebook into one coherent transparent and self-hostable platform roadmap.

**Architecture:** Documentation is split by decision type: navigation/thesis, market position, current capability evidence, target architecture, roadmap, measurement gates, and competitive monitoring. `README.md` and `requirements.md` remain concise entry points and link to canonical strategy documents instead of duplicating them.

**Tech Stack:** GitHub-flavored Markdown, Mermaid for architecture and flow diagrams, direct HTTPS citations, repository-relative links, `rg`/Git validation.

## Global Constraints

- Documentation-only branch: modify Markdown files only.
- Preserve the learning-first, hand-written C++20 engine vision and existing architectural boundaries.
- Treat the CPU renderer and SQLite mode as reference implementations, not obsolete code.
- Target solo developers and 2–5 person indie teams first; external developer beta is conditional.
- Prefer first-party or primary sources and date all unstable market claims as of 2026-07-11.
- Distinguish repository observation, external fact, vendor claim, and strategic inference.
- Do not claim production readiness for capabilities lacking deployment, migration, security, backup, observability, and rollback evidence.
- Do not recommend a public marketplace, payments, or creator payouts before their documented stage gates.
- No `TBD`, `TODO`, `FIXME`, empty sections, or unsourced market-size/adoption claims.

---

### Task 1: Strategy index and market position

**Files:**
- Create: `docs/strategy/README.md`
- Create: `docs/strategy/01-market-and-positioning.md`

**Interfaces:**
- Consumes: approved thesis and source policy from `docs/superpowers/specs/2026-07-11-game-platform-strategy-design.md`.
- Produces: canonical strategy navigation and the positioning assumptions referenced by all later documents.

- [ ] **Step 1: Create the strategy index**

Include: one-paragraph thesis; primary, secondary, and deferred users; document map;
current strategic decisions; evidence labels; annual/quarterly update cadence; glossary
for engine, Studio, BaaS, LiveOps, reference game, golden path, self-hosted, and stage gate.

- [ ] **Step 2: Write the market and positioning brief**

Include dated market signals; landscape groups (engines, backend platforms, server
hosting, creator ecosystems, distribution); buyer jobs; three strategic alternatives;
selected position; value proposition; honest strengths/weaknesses; opportunities,
threats, and strategic implications.

Required primary sources:

```text
https://newzoo.com/resources/trend-reports/newzoo-global-games-market-report-2025
https://gdconf.com/article/gdc-2026-state-of-the-game-industry-reveals-impact-of-layoffs-generative-ai-and-more/
https://unity.com/products/gaming-services/pricing
https://learn.microsoft.com/en-us/xbox/playfab/get-started/
https://onlineservices.epicgames.com/licensing
https://heroiclabs.com/nakama/
https://ir.roblox.com/news/news-details/2025/Roblox-Unveils-AI-Monetization-and-Performance-Innovations-for-Creators/
https://www.fortnite.com/news/fortnite-developers-will-soon-be-able-to-sell-in-game-items?lang=en-US
```

- [ ] **Step 3: Validate navigation and source labeling**

Run:

```bash
rg -n '^#|^##|https://' docs/strategy/README.md docs/strategy/01-market-and-positioning.md
rg -n '\b(TBD|TODO|FIXME)\b' docs/strategy/README.md docs/strategy/01-market-and-positioning.md
```

Expected: all required sections and sources are visible; placeholder search returns no matches.

- [ ] **Step 4: Commit the market foundation**

```bash
git add docs/strategy/README.md docs/strategy/01-market-and-positioning.md
git commit -m "docs(strategy): establish market position"
```

### Task 2: Current-state and capability-gap analysis

**Files:**
- Create: `docs/strategy/02-current-state-and-gap-analysis.md`

**Interfaces:**
- Consumes: current repository structure, README milestones, requirements constraints, CMake targets, SDK headers, BaaS routes/schema, and positioning from Task 1.
- Produces: evidence-based baseline and build/integrate decisions used by architecture and roadmap.

- [ ] **Step 1: Inventory current capability by layer**

Cover engine/platform, authoring/content, runtime/game examples, web delivery, BaaS,
SDK/developer experience, delivery/operations, documentation/testing. Use maturity
labels `Proven learning implementation`, `Functional prototype`, `Production seam`,
and `Absent` with explicit definitions.

- [ ] **Step 2: Build the buyer-relevant gap matrix**

For each layer record: current evidence, user impact, key gap, disposition
(`Build`, `Integrate`, `Defer`, `Do not pursue`), timing, and reason. Explicitly call
out the C++-only SDK, lack of API contract, lack of CI/deployment artifacts, SQLite
single-node posture, missing RBAC/audit/backups/traces, missing scene/resource project
model, and disconnected authoring scenes.

- [ ] **Step 3: Document structural risks and priority conclusions**

Include breadth-before-product risk, documentation drift, secret/trust-domain risk,
production-readiness labeling, public-marketplace complexity, rendering scope, and
single-maintainer sustainability. End with a ranked top-ten priority list tied to
golden-path impact.

- [ ] **Step 4: Validate repository evidence**

Run:

```bash
rg -n 'src/platform|src/engine|src/games|baas/|sdk/cpp|docs/book|CMakeLists' docs/strategy/02-current-state-and-gap-analysis.md
rg -n '\b(TBD|TODO|FIXME)\b' docs/strategy/02-current-state-and-gap-analysis.md
```

Expected: every layer cites a repository path; placeholder search returns no matches.

- [ ] **Step 5: Commit the gap analysis**

```bash
git add docs/strategy/02-current-state-and-gap-analysis.md
git commit -m "docs(strategy): assess platform capability gaps"
```

### Task 3: Target platform architecture

**Files:**
- Create: `docs/strategy/03-target-platform-architecture.md`

**Interfaces:**
- Consumes: approved architectural constraints, gap dispositions, and product loop.
- Produces: target boundaries and dependency rules used by roadmap initiatives.

- [ ] **Step 1: Define the target system context and golden path**

Add Mermaid diagrams for the six-layer platform and the create-to-operate sequence.
Define developer, operator, player, and external service actors. Explain the project
manifest, platform hub, CLI, Studio, runtime, build service, artifact registry, BaaS,
and observability responsibilities.

- [ ] **Step 2: Define control, content, data, and telemetry flows**

Document immutable resource/build identity, environment promotion, rollback, SDK
contract generation, player request flow, analytics exposure flow, and backup/restore.
Name ownership and failure behavior at each boundary.

- [ ] **Step 3: Preserve and extend existing seams**

Map target boundaries to `platform.hpp`, `assets::`, `Scene`/fixed tick, SDL-free core
libraries, `ITransport`/`IWsTransport`, and the separate BaaS process. Define new
boundaries: project manifest, resource registry, `RenderDevice`, API contract,
artifact store, identity/trust domains, and game-server provider adapter.

- [ ] **Step 4: Record build-vs-integrate decisions and quality attributes**

Build: learning engine, deterministic simulation, resource formats, project workflow,
SDK contract, local BaaS, guidebook. Integrate: TLS, SQL engine, object storage,
payments, voice, anti-cheat, email, cluster scheduling. Define portability,
determinism, security, recoverability, observability, performance, compatibility, and
operability acceptance rules.

- [ ] **Step 5: Validate Mermaid and terminology**

Run:

```bash
rg -n '^```mermaid|game.project|RenderDevice|ITransport|trust|rollback|restore' docs/strategy/03-target-platform-architecture.md
rg -n '\b(TBD|TODO|FIXME)\b' docs/strategy/03-target-platform-architecture.md
```

Expected: at least two Mermaid diagrams and every named boundary are present; no placeholders.

- [ ] **Step 6: Commit the target architecture**

```bash
git add docs/strategy/03-target-platform-architecture.md
git commit -m "docs(strategy): define target platform architecture"
```

### Task 4: Outcome-gated roadmap

**Files:**
- Create: `docs/strategy/04-roadmap-2026-2029.md`

**Interfaces:**
- Consumes: target architecture, top-ten gaps, and stage-gate principle.
- Produces: sequenced vertical initiatives and decision triggers.

- [ ] **Step 1: Define roadmap rules and workstreams**

Use six workstreams: platform spine, engine/runtime, Studio/content, BaaS/LiveOps,
delivery/operations, and learning/ecosystem. State capacity assumptions for a small
team and require one primary vertical slice per horizon.

- [ ] **Step 2: Write Horizons 0–4**

For each horizon include outcome, included initiatives, dependencies, reference-game
proof, measurable exit gate, risks, and explicit non-goals. Use the approved windows:
0–3, 3–6, 6–12, 12–24, and 24–36 months as planning horizons rather than promises.

- [ ] **Step 3: Add dependency and trigger maps**

Show that contract/manifest/resource identity precede hub automation; packaging
precedes publishing; observability/rollback precede external beta; external beta
precedes public catalog. Define evidence triggers for WebGPU, authoritative hosting,
mainstream engine SDK, browser authoring, public marketplace, and commerce.

- [ ] **Step 4: Add the next-90-day sequence**

Provide ordered documentation/architecture milestones with acceptance outcomes, not
implementation pseudo-code. Include strategy adoption, manifest ADR, API inventory,
CI baseline, reference-game selection, golden-path measurement, and platform-hub
information architecture.

- [ ] **Step 5: Validate dependencies and gates**

Run:

```bash
rg -n 'Horizon [0-4]|Exit gate|Dependencies|Non-goals|trigger|90 days' docs/strategy/04-roadmap-2026-2029.md
rg -n '\b(TBD|TODO|FIXME)\b' docs/strategy/04-roadmap-2026-2029.md
```

Expected: all five horizons, exit gates, triggers, and next-90-day plan are present; no placeholders.

- [ ] **Step 6: Commit the roadmap**

```bash
git add docs/strategy/04-roadmap-2026-2029.md
git commit -m "docs(strategy): add outcome-gated platform roadmap"
```

### Task 5: Product metrics and stage gates

**Files:**
- Create: `docs/strategy/05-product-metrics-and-stage-gates.md`

**Interfaces:**
- Consumes: target users, golden path, release flow, and roadmap exit gates.
- Produces: metric definitions and go/no-go rules that prevent roadmap-by-opinion.

- [ ] **Step 1: Define the north-star behavior and metric tree**

Use `Verified Iteration`: a project completes authoring, deterministic validation,
immutable preview publication, and an observed feedback event within seven days.
Explain why this behavior connects learning, creation, shipping, and operations.

- [ ] **Step 2: Specify metric contracts**

For activation, creation, quality, publishing, operations, learning, and ecosystem,
define formula, event source, dimensions, cadence, accountable role, baseline method,
and decision threshold. Avoid vanity metrics.

- [ ] **Step 3: Define stage gates and guardrails**

Add gates for platform-spine completion, publish-loop readiness, production pilot,
external beta, curated catalog, and commerce exploration. Include security, cost,
moderation, reliability, and documentation guardrails.

- [ ] **Step 4: Define instrumentation debt and dashboard views**

Map required local CLI spans, build events, artifact events, BaaS request metrics,
player analytics, experiment exposures, and support incidents. Specify owner, storage,
retention, and privacy posture without inventing current measurements.

- [ ] **Step 5: Validate formulas and thresholds**

Run:

```bash
rg -n 'Formula|Source|Cadence|Owner|Threshold|Gate|Guardrail|Verified Iteration' docs/strategy/05-product-metrics-and-stage-gates.md
rg -n '\b(TBD|TODO|FIXME)\b' docs/strategy/05-product-metrics-and-stage-gates.md
```

Expected: each metric has an operational definition and each later phase has a gate; no placeholders.

- [ ] **Step 6: Commit the measurement model**

```bash
git add docs/strategy/05-product-metrics-and-stage-gates.md
git commit -m "docs(strategy): define product metrics and gates"
```

### Task 6: Competitive watchlist

**Files:**
- Create: `docs/strategy/06-competitive-watchlist.md`

**Interfaces:**
- Consumes: chosen position, capability model, and dated primary research.
- Produces: honest comparison and repeatable monitoring process.

- [ ] **Step 1: Define competitor groups and weighted buyer criteria**

Groups: Unity/Unreal/Godot; UGS/PlayFab/EOS; Nakama/Beamable/Pragma;
GameLift/Edgegap/Agones; Roblox/UEFN; itch.io/Steam. Criteria prioritize transparent
learning, local/self-hosted workflow, authoring-to-operations continuity, web preview,
production operations, ecosystem, and team adoption.

- [ ] **Step 2: Build comparison matrices**

Use ratings `Strong`, `Adequate`, `Weak`, `Absent`, `Not targeted`, each backed by a
short reason. Ensure established competitors clearly win production scale, SDK reach,
editor breadth, hosting, social/trust, distribution, or ecosystem where appropriate.

- [ ] **Step 3: Write profiles and strategic implications**

For each group cover positioning, strengths, weaknesses or trade-offs, threat,
learning opportunity, and response (`Differentiate`, `Reach parity`, `Integrate`,
`Monitor`, `Ignore`). Add nightmare scenarios and signals that would invalidate the
selected position.

- [ ] **Step 4: Add monitoring cadence and change log template**

Monthly: pricing/status changes. Quarterly: capability and positioning. Annually:
market assumptions and roadmap. Define source owner by role, evidence date, and the
threshold for opening an ADR or strategy revision.

- [ ] **Step 5: Validate balance and citations**

Run:

```bash
rg -n 'Strong|Adequate|Weak|Absent|Not targeted|Differentiate|Integrate|Monitor|Ignore' docs/strategy/06-competitive-watchlist.md
rg -n 'https://' docs/strategy/06-competitive-watchlist.md
rg -n '\b(TBD|TODO|FIXME)\b' docs/strategy/06-competitive-watchlist.md
```

Expected: balanced ratings, primary links, response decisions, and no placeholders.

- [ ] **Step 6: Commit the watchlist**

```bash
git add docs/strategy/06-competitive-watchlist.md
git commit -m "docs(strategy): add competitive watchlist"
```

### Task 7: Entry points, cross-document audit, and final verification

**Files:**
- Modify: `README.md`
- Modify: `requirements.md`
- Modify if required for broken links or contradictions: `docs/strategy/*.md`

**Interfaces:**
- Consumes: all strategy documents.
- Produces: discoverable canonical strategy package with verified links and clean Git scope.

- [ ] **Step 1: Add a concise strategy section to README**

After the roadmap table, link the strategy index, market position, gap analysis,
target architecture, roadmap, metrics, and watchlist. Explain that completed milestone
rows remain implementation history while strategy docs guide the next horizons.

- [ ] **Step 2: Extend requirements without rewriting the original vision**

Add a final section named `12. Chiến lược platform 2026–2029` that preserves the
learning-first constraints, records the selected product position, names the golden
path, lists non-negotiable strategy principles, and links to canonical documents.

- [ ] **Step 3: Run placeholder, link-target, and scope checks**

Run:

```bash
git diff --check
rg -n '\b(TBD|TODO|FIXME)\b' docs/strategy README.md requirements.md
for f in $(rg -o '\]\(([^)]+\.md)(#[^)]+)?\)' docs/strategy README.md requirements.md | sed -E 's/.*\]\(([^)#]+\.md).*/\1/' | sort -u); do test -f "$f" || test -f "docs/strategy/$f" || printf 'missing: %s\n' "$f"; done
git status --short
```

Expected: `git diff --check` passes; placeholder search is empty; link loop prints no
`missing:` lines; status contains Markdown files only.

- [ ] **Step 4: Audit consistency and acceptance coverage**

Check every section of the approved design spec against the document package. Confirm
the user, position, principles, capability model, five horizons, metrics, stage gates,
source policy, risks, and non-goals each have a canonical home. Resolve contradictions
inline and avoid duplicating detailed subsystem docs.

- [ ] **Step 5: Run documentation-only verification**

Run:

```bash
git diff --name-only HEAD | rg -v '\.md$'
git diff --stat
git log --oneline --decorate -8
```

Expected: non-Markdown search returns no matches; diff contains only intended docs;
history shows one focused commit per documentation concern.

- [ ] **Step 6: Commit entry points and final corrections**

```bash
git add README.md requirements.md docs/strategy
git commit -m "docs(strategy): connect platform strategy entry points"
```
