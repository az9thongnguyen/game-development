# Horizon 0 — Sequencing Plan ("make it one product")

**Owner posture:** _blend_ (see [strategy 04](../../strategy/04-roadmap-2026-2029.md)) —
one thin golden path first, interleave a hand-written runtime slice to keep momentum,
later ops-heavy horizons stay conditional.

**Done so far:** platform strategy adopted; **slice 1** project manifest + native golden
path (`0b60dfd`); **slice 2** web parity `?mode=project` (`cc724cb`).

This plan orders the *remaining* Horizon 0 work by dependency, value, and — importantly —
**what can be fully verified in this environment** vs what needs the owner or external
infra. Slices marked ⚙️ are executed autonomously (headless-testable); 🙋 need the owner
(browser/render) or external infra (CI runners).

## Sequence

### ⚙️ Slice 3 — Resource declarations + content hash + dependency closure  (priority #2)
A project should declare its content and refuse to ship if any is missing.
- `resource_core`: hand-written `content_hash` (FNV-1a, deterministic) — the content-hash
  primitive the architecture requires; unit-tested.
- Manifest gains repeatable additive `asset <type> <path>` lines → `Project.assets`.
- `--project-inspect` resolves each asset via `assets::`, reports present/missing + hash;
  `--project` refuses to launch on a missing dependency (closure = hard reject, metric C3).
- Tests: asset-line parse/round-trip, `content_hash` determinism/known-vector, closure.
- **Value:** the seed of packaging + the resource identity the registry/preview need.

### ⚙️ Slice 4 — Input-action mapping  (blend: the "fun" runtime slice)
Interleaved hand-written runtime work serving the reference FPS game.
- `input_core`: pure map raw keys → named actions (MoveForward/Back, StrafeL/R, TurnL/R,
  Fire), rebindable table, headless-testable (feed a key set → get the action set).
- Refactor `fps::RaycastScene` to read actions instead of raw `Key::W` etc.
- Tests: default binding maps expected keys; rebinding; unknown key ignored.
- **Value:** input profiles are a named Horizon 0 runtime contract; keeps momentum.

### ⚙️ Slice 5 — CI workflow file  (write only; runs on push)
- `.github/workflows/ci.yml`: clean-checkout configure + build + `ctest` on macOS/Linux.
- Honest limit: this environment has no GitHub runner, so the file is authored and
  self-reviewed but its green run is verified by the owner after push. 🙋 for the run.

### ⚙️ Slice 6 — Golden-path rehearsal record
- A short `docs/strategy/horizon-0-rehearsal.md` capturing the current clean-path timing
  and top friction (the roadmap's "Days 1–15 evidence pack"), from real local runs.

### 🙋 Browser accept — web golden path
- Drive Chrome against `build-web` `demo.html?mode=project`, confirm the FPS view renders,
  capture a GIF. Closes slice 2's one manual accept. Offered separately (browser-automation).

### Deferred to the Horizon 0→1 boundary (not now)
- BaaS OpenAPI contract inventory (large; belongs with the SDK conformance work).
- Resource *migration* framework (no second schema yet — YAGNI until one exists).
- Studio shell (Horizon 1 by the roadmap).

## Exit check for Horizon 0 (blend-scoped)
A clean checkout: creates/uses the reference project; validates project + resource
closure before build; runs the reference game native and web from one named manifest
with no source edit; `ctest` green; CI file present. The heavier gates (5 timed
rehearsals, full CI matrix green) close once the owner runs CI + the browser accept.
