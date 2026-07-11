# Product Metrics and Stage Gates

**Model date:** 2026-07-11
**Measurement status:** Definitions and initial decision thresholds; current baselines
must be collected before targets are interpreted as performance claims.
**Purpose:** Fund product outcomes and safe operation rather than feature count.

## North-star behavior: Verified Iteration

A **Verified Iteration** occurs when one project completes all four events within a
rolling seven-day window:

1. an authored source/resource change is saved with a valid schema;
2. deterministic validation and the project's required tests pass;
3. an immutable preview release is published from locked inputs;
4. at least one governed feedback event is observed for that release—a tester session,
   explicit review, supported player event, or operator health assessment.

The north-star metric is:

```text
Weekly Verified Projects =
  distinct project_id values with >= 1 Verified Iteration in the last 7 days
```

For an owner-only learning phase, a project can count through a documented self-review
session. External beta requires a feedback actor other than the release publisher.

### Why this is the north star

- It joins creation, engineering quality, distribution, and learning.
- It cannot increase through account creation or asset spam alone.
- It rewards a repeatable loop rather than a one-time launch.
- It makes missing platform connections visible: authoring without packaging, builds
  without publishing, or releases without feedback do not count.
- It remains meaningful in local/self-hosted deployments where revenue is not the
  primary objective.

It is not a direct measure of game quality or commercial success. Retention, player
safety, reliability, and cost remain guardrails.

## Measurement principles

1. **Instrument the golden path before optimizing it.** Unknown baseline is reported
   as unknown, never replaced by an estimate presented as fact.
2. **One semantic event, many sinks.** Local logs, tests, and production analytics use
   the same event contract where privacy allows.
3. **Release and version dimensions are mandatory.** A metric without project,
   environment, release, platform, tool/API schema, and result class cannot diagnose
   regressions.
4. **Separate developer telemetry from player analytics.** Consent, retention,
   identity, access, and purpose differ.
5. **Distribution matters.** Medians alone can hide beginners, slow machines, regions,
   and failing projects; report p75/p95 and failure classes.
6. **Guardrails can veto growth.** More iterations are not success if crash, data loss,
   abuse, cost, or support load exceeds limits.
7. **No individual productivity surveillance.** Measure workflow and system behavior;
   do not rank contributors by keystrokes, commits, or hours.

## Evidence maturity

Each metric is labeled:

- **Defined:** formula and events exist only in documentation.
- **Instrumented:** events are emitted and validated in local/test environments.
- **Baselined:** enough representative observations exist to describe current behavior.
- **Gated:** a reviewed threshold controls a roadmap decision.
- **Operational:** owner, dashboard, alert/review cadence, and data-quality checks exist.

Moving directly from Defined to Gated without a baseline is allowed only for binary
safety requirements such as “restore drill passes” or “no secret in client package.”

## Canonical event envelope

All platform workflow events should use a versioned envelope:

```text
event_id             globally unique, retry-safe
event_name           stable semantic name
event_schema_version integer
occurred_at           UTC event time
recorded_at           UTC ingestion time
project_id            opaque stable ID
environment           local | preview | production
release_id            immutable ID when applicable
platform_version      tool/platform release
project_schema        manifest schema version
actor_type            developer | ci | operator | player | system
actor_id_hash         optional, purpose-scoped pseudonymous ID
session_id            workflow or player session ID
correlation_id        links commands, requests, builds, and diagnostics
result                success | rejected | failed | cancelled
error_class           stable class, never raw secret/private content
duration_ms           monotonic measured duration when applicable
properties            event-specific validated fields
```

Required behavior:

- duplicate `event_id` ingestion is idempotent;
- clocks and ingestion lag are monitored;
- raw command lines, file contents, tokens, emails, save data, and secrets are excluded;
- local telemetry defaults are documented and controllable;
- event version changes include compatibility and retention decisions.

## Metric contracts

The thresholds below are initial stage-gate decisions. They are not current results.
After the first baseline, a threshold can change only with a recorded reason and no
retroactive rewriting of prior gate outcomes.

### Activation

#### A1. Golden-path completion rate

| Field | Contract |
|---|---|
| **Formula** | Distinct rehearsals reaching first successful browser preview ÷ distinct rehearsals starting project setup |
| **Source** | `project_workflow_started`, `preview_started` |
| **Dimensions** | OS, architecture, clean/existing checkout, platform version, template, experience band (self-declared/optional) |
| **Cadence** | Per rehearsal; weekly review during Horizons 0–1 |
| **Owner** | Developer experience owner |
| **Initial Threshold** | Horizon 0 gate: 5/5 controlled clean-checkout rehearsals complete; external beta gate: ≥80% unassisted completion across at least 10 starts |

#### A2. Time to first browser preview

| Field | Contract |
|---|---|
| **Formula** | `preview_started.occurred_at - project_workflow_started.occurred_at`, excluding explicitly recorded human pause |
| **Source** | CLI/Hub workflow events |
| **Dimensions** | OS, template, cold/warm dependency cache, success/failure |
| **Cadence** | Per run; median, p75, p95 weekly |
| **Owner** | Developer experience owner |
| **Initial Threshold** | Horizon 0: baseline recorded; Horizon 1: controlled p50 ≤15 minutes from an existing project content change to shared preview; beta: p50 ≤30 minutes from clean template start |

#### A3. Actionable diagnostic rate

| Field | Contract |
|---|---|
| **Formula** | Failed commands with stable error class, next action, and documentation link ÷ all failed golden-path commands |
| **Source** | CLI diagnostic result events and error catalog |
| **Dimensions** | command, error class, platform version |
| **Cadence** | Every CI run; weekly failure review |
| **Owner** | CLI/diagnostics owner |
| **Initial Threshold** | 100% for the top ten observed golden-path failure classes before external beta |

### Creation and content quality

#### C1. Time to first valid authored resource

| Field | Contract |
|---|---|
| **Formula** | First `resource_validation_passed` for a newly created resource minus first Studio project open |
| **Source** | Studio document and validator events |
| **Dimensions** | resource type, template, first-time/repeat user, tool version |
| **Cadence** | Per authoring session; monthly review |
| **Owner** | Studio owner |
| **Initial Threshold** | Establish baseline in Horizon 1; beta p50 ≤10 minutes for a guided texture or map task |

#### C2. Resource validation pass rate

| Field | Contract |
|---|---|
| **Formula** | Unique resource versions passing validation ÷ unique resource versions submitted for validation |
| **Source** | Resource validator |
| **Dimensions** | resource type/schema/tool version/error class |
| **Cadence** | Per validation; weekly trend |
| **Owner** | Resource-system owner |
| **Initial Threshold** | No universal quality target; block release on any required-resource failure and drive top recurring authoring errors down quarter over quarter |

#### C3. Package dependency completeness

| Field | Contract |
|---|---|
| **Formula** | Release candidates whose full dependency closure resolves and hashes ÷ all release candidates |
| **Source** | Packager manifest and validator |
| **Dimensions** | project, profile, resource schema, missing/cycle/incompatible error |
| **Cadence** | Every package |
| **Owner** | Resource/build owner |
| **Initial Threshold** | 100% for every published release; unresolved dependency is a hard rejection |

#### C4. Recovery integrity

| Field | Contract |
|---|---|
| **Formula** | Crash/autosave recovery rehearsals restoring last committed source plus documented recoverable edits ÷ rehearsals attempted |
| **Source** | Studio recovery test suite and manual failure drills |
| **Dimensions** | document type, failure point, platform |
| **Cadence** | Release candidate and quarterly drill |
| **Owner** | Studio owner |
| **Initial Threshold** | 100% of declared recovery scenarios before Studio is recommended for external projects |

### Engineering and runtime quality

#### Q1. Clean-checkout build success

| Field | Contract |
|---|---|
| **Formula** | CI jobs completing configure/build/required tests ÷ CI jobs started on supported matrix cells |
| **Source** | CI workflow results |
| **Dimensions** | OS, compiler, architecture, native/Web, cache state, commit |
| **Cadence** | Every change; weekly reliability review |
| **Owner** | Build/release owner |
| **Initial Threshold** | 100% required cells on release commits; flaky retry does not convert original failure into success |

#### Q2. Required test pass rate

| Field | Contract |
|---|---|
| **Formula** | Required test cases passed ÷ required test cases executed, with skipped tests reported separately |
| **Source** | CTest/JUnit-style result artifact |
| **Dimensions** | suite, platform, sanitizer, unit/integration/browser |
| **Cadence** | Every CI run |
| **Owner** | Component owners; release owner for gate |
| **Initial Threshold** | 100%; required failures or unexplained skips block release |

#### Q3. Reproducible artifact rate

| Field | Contract |
|---|---|
| **Formula** | Rebuilds from identical locked inputs producing declared-equivalent manifests/hashes ÷ reproducibility checks |
| **Source** | Build provenance and artifact manifest comparator |
| **Dimensions** | target, toolchain, expected nondeterministic fields |
| **Cadence** | Nightly or release candidate |
| **Owner** | Build/release owner |
| **Initial Threshold** | 100% manifest/resource equivalence before immutable release gate; binary identity only where toolchain supports it |

#### Q4. Reference-game budget compliance

| Field | Contract |
|---|---|
| **Formula** | Frames/startups/packages meeting the versioned profile budget ÷ measured samples |
| **Source** | Runtime benchmark and package manifest |
| **Dimensions** | reference game, device class, backend, resolution, release |
| **Cadence** | Release candidate; trend per commit where stable |
| **Owner** | Runtime owner |
| **Initial Threshold** | Budgets are baselined in Horizon 0 and frozen by profile ADR; regression beyond budget blocks promotion unless explicitly waived with expiry |

### Publishing and release safety

#### P1. Publish success rate

| Field | Contract |
|---|---|
| **Formula** | Publish operations returning a verified immutable release ID ÷ publish operations started |
| **Source** | Release service events |
| **Dimensions** | target, environment, failure class, retry/cold start |
| **Cadence** | Every publish; weekly review |
| **Owner** | Release owner |
| **Initial Threshold** | ≥95% in controlled Horizon 1 rehearsals; partial uploads exposed as releases = 0 |

#### P2. Preview parity

| Field | Contract |
|---|---|
| **Formula** | Published preview manifests whose resource hashes equal the locally approved package ÷ published previews |
| **Source** | Local and registry manifest comparator |
| **Dimensions** | project, release, target |
| **Cadence** | Every preview publish |
| **Owner** | Release owner |
| **Initial Threshold** | 100%; mismatch is a hard failure |

#### P3. Promotion and rollback time

| Field | Contract |
|---|---|
| **Formula** | Channel target verified time minus authorized action start time |
| **Source** | Audit and synthetic release-health events |
| **Dimensions** | environment, promote/rollback, reason, release size |
| **Cadence** | Every operation; quarterly drill |
| **Owner** | Game operator |
| **Initial Threshold** | p95 ≤5 minutes during Horizon 1 drills; failed rollback = 0 |

#### P4. Change failure rate

| Field | Contract |
|---|---|
| **Formula** | Production promotions requiring rollback, emergency fix, or incident within 24 hours ÷ production promotions |
| **Source** | Audit, incident, and release events |
| **Dimensions** | project, release, failure class, content/code/config |
| **Cadence** | Per release; monthly review |
| **Owner** | Release owner and operator |
| **Initial Threshold** | Baseline during pilot; any data-loss/security change failure is a gate failure regardless of percentage |

### BaaS reliability and recovery

#### O1. Service availability

| Field | Contract |
|---|---|
| **Formula** | Successful synthetic golden-path checks ÷ scheduled checks, excluding approved maintenance only when declared in advance |
| **Source** | External synthetic monitor |
| **Dimensions** | environment, region, route/journey, release |
| **Cadence** | 1–5 minute checks; weekly SLO review |
| **Owner** | Service operator |
| **Initial Threshold** | Pilot guardrail ≥99.5% over rolling 28 days; external beta target decided after pilot evidence |

#### O2. API error and latency

| Field | Contract |
|---|---|
| **Formula** | Server-error rate = 5xx ÷ eligible requests; latency = p50/p95/p99 completed request duration |
| **Source** | Edge/BaaS metrics with normalized routes |
| **Dimensions** | project, environment, route, status class, release, region |
| **Cadence** | Continuous; daily during pilot |
| **Owner** | Service operator and domain owner |
| **Initial Threshold** | Pilot: 5xx <1% and p95 <500 ms for non-realtime CRUD at expected load; per-route budgets override aggregate |

#### O3. Trace/correlation coverage

| Field | Contract |
|---|---|
| **Formula** | Eligible external requests linked to release/project/request trace context and terminal result ÷ eligible requests |
| **Source** | Trace/log validation job |
| **Dimensions** | route, SDK, platform, environment |
| **Cadence** | Daily and release candidate |
| **Owner** | Observability owner |
| **Initial Threshold** | ≥99% before production pilot; 100% for operator/economy/audit actions |

#### O4. Recovery point and recovery time

| Field | Contract |
|---|---|
| **Formula** | RPO = incident time minus newest verified recoverable point; RTO = restore authorization to passing isolated golden-path check |
| **Source** | Backup metadata and restore drill |
| **Dimensions** | data store, environment, backup class, failure scenario |
| **Cadence** | Scheduled backup; monthly/quarterly restore drill by phase |
| **Owner** | Data/service operator |
| **Initial Threshold** | Pilot: RPO ≤24 hours and RTO ≤4 hours; tighter values require user/business evidence and budget |

#### O5. Cost per active project/player

| Field | Contract |
|---|---|
| **Formula** | Allocated monthly infrastructure and third-party cost ÷ active projects; also ÷ monthly active players for player-serving services |
| **Source** | Provider bills, resource tags, usage meters |
| **Dimensions** | environment, project, service, storage/compute/egress, fixed/variable |
| **Cadence** | Monthly; weekly anomaly check during pilots |
| **Owner** | Platform operator/product owner |
| **Initial Threshold** | Establish explicit pilot budget before launch; >20% unplanned monthly variance pauses scale expansion until explained |

### LiveOps and data quality

#### L1. Exposure integrity

| Field | Contract |
|---|---|
| **Formula** | Assigned experiment evaluations with exactly one valid exposure record and compatible release/schema ÷ assignments |
| **Source** | Assignment service and analytics reconciliation |
| **Dimensions** | experiment, variant, release, SDK, environment |
| **Cadence** | Daily while active |
| **Owner** | LiveOps/data owner |
| **Initial Threshold** | ≥99%; conflicting assignment or missing denominator blocks decision use |

#### L2. LiveOps rollback readiness

| Field | Contract |
|---|---|
| **Formula** | Active config/event/experiment versions with a validated predecessor and successful stop/rollback drill ÷ active versions requiring rollback |
| **Source** | LiveOps audit and drill events |
| **Dimensions** | change type, project, environment |
| **Cadence** | Before activation; quarterly drill |
| **Owner** | Game operator |
| **Initial Threshold** | 100% for production changes |

#### L3. Event schema validity

| Field | Contract |
|---|---|
| **Formula** | Ingested events passing registered schema and required semantic checks ÷ received events |
| **Source** | Ingestion validation/dead-letter summary |
| **Dimensions** | event name/version, SDK, release, rejection class |
| **Cadence** | Continuous; daily review |
| **Owner** | Data owner and producing domain |
| **Initial Threshold** | ≥99.9% for supported SDKs; rejected events remain observable without storing prohibited payloads |

### Learning and external adoption

#### E1. Journey completion

| Field | Contract |
|---|---|
| **Formula** | Opt-in learners completing a journey acceptance command ÷ opt-in learners starting it |
| **Source** | Local acceptance token or privacy-preserving voluntary telemetry |
| **Dimensions** | journey, platform version, experience band, failure class |
| **Cadence** | Per release; quarterly review |
| **Owner** | Documentation/developer experience owner |
| **Initial Threshold** | Baseline before external beta; top abandonment step gets an owner each quarter |

#### E2. External unassisted golden-path completion

| Field | Contract |
|---|---|
| **Formula** | External teams completing first publish without synchronous author intervention ÷ teams starting onboarding |
| **Source** | Beta cohort log and support records |
| **Dimensions** | team size, OS, template, self-hosted/hosted adapter |
| **Cadence** | Per beta cohort |
| **Owner** | Developer experience/product owner |
| **Initial Threshold** | Horizon 3 gate: 3 teams complete; rate ≥80% once cohort has at least 10 starts |

#### E3. Second verified iteration rate

| Field | Contract |
|---|---|
| **Formula** | External projects completing a second Verified Iteration within 30 days ÷ external projects completing the first |
| **Source** | Verified Iteration events and beta cohort registry |
| **Dimensions** | template, team, project type, failure/abandon reason |
| **Cadence** | Monthly |
| **Owner** | Product owner |
| **Initial Threshold** | Horizon 3 gate: at least 2 of the first 3 teams; later target recalibrated after cohort baseline |

#### E4. Maintainer support load

| Field | Contract |
|---|---|
| **Formula** | Maintainer hours spent on user-specific support, incidents, and manual recovery ÷ total maintainer capacity |
| **Source** | Lightweight issue/support time categories, not individual surveillance |
| **Dimensions** | support class, product area, docs gap, defect, environment |
| **Cadence** | Weekly during beta; monthly otherwise |
| **Owner** | Product/maintainer lead |
| **Initial Threshold** | If support exceeds 30% of capacity for four weeks, pause onboarding and address top systemic causes |

## Stage gates

### Gate 0 — Platform-spine completion

Required evidence:

- 5/5 controlled clean-checkout golden-path rehearsals complete;
- project/resource compatibility is checked before build;
- native/Web build and required tests use one named project;
- supported CI cells are green with provenance;
- no source edit is needed to choose the reference project/scene;
- baseline timing and top failure classes are recorded.

Decision: authorize create-to-publish implementation, or remediate the failed criteria.

### Gate 1 — Publish-loop readiness

Required evidence:

- exact local/published preview manifest parity = 100%;
- partial release exposure = 0;
- publish success ≥95% in controlled rehearsals;
- p95 promotion/rollback ≤5 minutes and all rollback drills pass;
- five rehearsals achieve p50 author-to-share ≤15 minutes;
- required browser smoke matrix passes.

Decision: authorize a limited production pilot, not external developer beta.

### Gate 2 — Production pilot

Required evidence:

- reproducible deployment and compatible migration;
- verified restore meets RPO ≤24 hours and RTO ≤4 hours;
- availability ≥99.5%, 5xx <1%, relevant p95 latency <500 ms for four weeks at
  expected pilot load;
- trace coverage ≥99%, 100% for operator/economy/audit actions;
- bad release and LiveOps rollback drills pass;
- experiment exposure integrity and schema validity meet thresholds;
- secrets/trust review finds no privileged credential in client/Web artifacts;
- cost remains within approved pilot budget.

Decision: authorize external developer beta only if maintainer capacity also exists.

### Gate 3 — External developer beta

Required evidence:

- three external teams complete the golden path unassisted;
- at least two complete a second Verified Iteration within 30 days;
- supported previous-version upgrade/migration passes;
- support load stays ≤30% of capacity for eight weeks;
- security response, documentation freshness, and hosting cost meet their guardrails;
- requests reinforce a focused position rather than general-engine parity.

Decision: continue/narrow beta or evaluate a curated catalog. This gate does not
authorize public self-service uploads.

### Gate 4 — Curated catalog

Required evidence before starting:

- at least five external projects have two verified releases;
- at least three show repeat-player behavior over eight weeks;
- registry ownership/versioning and emergency unpublish are reliable;
- moderation/support owner and budget are named;
- content policy, reporting, takedown, appeal, scanning, and provenance design passes
  review.

Required evidence before expansion:

- catalog improves repeat creation or repeat play versus private sharing;
- abuse/moderation response and false positives meet declared targets;
- hosting and operator cost stay within budget;
- compromise, malicious upload, takedown, appeal, and rollback drills pass.

Decision: expand curation, remain private, or stop. Commerce requires a separate gate.

### Gate 5 — Commerce exploration

Required evidence:

- the catalog creates sustained player/creator value without payments;
- economy ledger, idempotency, server authority, receipt validation, refunds, and fraud
  threat model are proven;
- legal entity, tax, consumer support, regional availability, privacy, and creator
  terms have accountable owners;
- conservative unit economics include store fees, hosting, moderation, fraud, refunds,
  support, and payout operations.

Decision: open a separately scoped commerce design or explicitly decline. Revenue
desire alone does not pass the gate.

## Guardrails

### Security and privacy

- zero known critical/high exploitable findings in a release;
- no secret or private player payload in client packages, logs, analytics, or
  diagnostic bundles;
- 100% audit coverage for privileged changes;
- export/delete and retention procedures verified before external player data.

### Reliability and recovery

- required tests and compatibility checks pass;
- no promotion without a verified predecessor or explicit first-release procedure;
- backup success is not reported as restore success;
- any data-loss incident pauses expansion regardless of availability percentage.

### Cost and capacity

- every hosted phase has a fixed and variable cost budget;
- unplanned >20% monthly variance requires explanation and mitigation;
- support/incident load >30% maintainer capacity for four weeks pauses onboarding;
- additional supported platforms require recurring CI/security/docs ownership.

### Trust and ecosystem

- public discovery does not begin without moderation/takedown/appeal capability;
- AI-generated or transformed assets carry provenance and policy status;
- vanity growth never overrides creator/player safety or sustainable operations;
- child-directed services require dedicated consent and safety design, not generic
  account flags.

## Dashboard views

### Developer workflow dashboard

- starts, completion, time to first preview/publish;
- failure funnel by command/error/platform/version;
- build/test/package duration and artifact size;
- diagnostic coverage and documentation link success;
- Verified Iterations by project and reference template.

### Release dashboard

- release manifest/provenance/compatibility;
- publish success and failure classes;
- channel targets, promotions, rollbacks, change failure rate;
- browser/runtime smoke health and budget regressions.

### Service operations dashboard

- golden-path availability;
- route error/latency/saturation;
- trace/correlation coverage;
- migration, backup age, restore-drill status, RPO/RTO;
- security/audit anomalies and cost allocation.

### LiveOps/data dashboard

- active versions/segments/experiments and rollback readiness;
- assignment/exposure integrity;
- event schema rejection/lag/duplicates;
- guardrail metrics before outcome interpretation.

### External beta/ecosystem dashboard

- unassisted completion and second iteration;
- support load and systemic failure themes;
- project/player repeat behavior;
- moderation/abuse/cost only when a catalog phase exists.

## Instrumentation debt register

| Required signal | Current state | First collection point | Needed by |
|---|---|---|---|
| Project workflow start/complete | Absent | CLI/Hub command domain | Horizon 0 baseline |
| Structured diagnostics | Partial text output | CLI result envelope | Horizon 0 gate |
| Build provenance/test artifact | Absent as CI contract | Build pipeline | Horizon 0 gate |
| Resource validation/dependency events | Test output only | Resource validator | Horizon 1 |
| Release publish/promotion/rollback audit | Absent | Artifact/release domain | Horizon 1 gate |
| Browser synthetic health | Manual | Browser test runner/external probe | Horizon 1 gate |
| Request trace context | Access logs/metrics partial | SDK, edge, BaaS middleware | Horizon 2 |
| Backup/restore evidence | Absent | Data operations command | Horizon 2 gate |
| Experiment assignment/exposure | Absent | LiveOps evaluation + SDK | Horizon 2 gate |
| External completion/support load | Absent | Beta cohort workflow | Horizon 3 |
| Catalog trust/cost signals | Not targeted | Catalog/moderation systems | Horizon 4 only |

## Ownership model

In a one-person project, roles may map to one human but reviews remain distinct hats:

- **Product owner:** users, position, stage-gate decision, budget.
- **Developer experience owner:** project/CLI/Hub/onboarding and workflow metrics.
- **Runtime/Studio owner:** resource quality, determinism, performance, recovery.
- **Release owner:** CI, provenance, artifacts, channels, rollback.
- **Service/data operator:** availability, migrations, backup, telemetry, cost.
- **Security/trust owner:** threat model, credentials, audit, privacy, abuse.
- **Documentation owner:** journey correctness, version freshness, error links.

A gate cannot be self-certified solely by the implementer hat when it includes
security, recovery, or external-user evidence. The review record states which hats
were exercised and what evidence was inspected.

## Metric change policy

Change a definition or threshold only when:

1. the existing metric is ambiguous, gameable, unactionable, or misaligned;
2. evidence shows the threshold is unsafe or unrelated to user value;
3. supported scope changes materially;
4. privacy/cost requires a safer collection method.

Record the old/new definition, reason, effective date, comparability impact, and
affected gates. Never rewrite a historical failure as a pass under a new definition.

## What is deliberately not measured

- individual developer ranking, keystrokes, raw working hours, or commit count;
- total feature/module/chapter count as product success;
- registered users without activation;
- raw asset uploads without valid package/use;
- total events ingested without schema quality or decision purpose;
- player engagement optimized without safety, consent, cost, and game-quality context;
- AI generations as value unless they contribute to a verified, provenance-safe loop.

## Measurement success condition

The metrics system succeeds when a roadmap review can answer, with evidence:

- Can a new user complete the supported path?
- Where and why do they fail?
- Is the exact tested content what was published?
- Can a bad change be detected and reversed?
- Can data be restored and behavior traced?
- Did a developer complete another iteration?
- Are reliability, security, trust, cost, and maintainer capacity still acceptable?

If the answer depends on memory or manual interpretation of unrelated logs, the
platform is not yet measurable enough for the next stage.
