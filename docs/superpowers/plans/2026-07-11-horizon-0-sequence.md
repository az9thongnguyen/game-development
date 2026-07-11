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

---

## Horizon 1 — the immutable release store (DONE, `docs/book/93`)
The Horizon 0→1 bridge (`--project-package`) produced a `packagehash`; Horizon 1 plants
that seed. Shipped as one coherent slice (pure core + wired verbs + lifecycle smoke):

- **`release_core`** (`src/engine/release/release.{hpp,cpp}`, pure/headless): content-
  addressed path scheme (`releases/<hash>/package.txt`), channel pointer format
  (`channel1 <hash>`), and the two trust-boundary validators (`valid_channel_name`,
  `valid_hash_hex`) — because `--release-rollback`'s args become filesystem paths.
- **`assets::write_file`** now `create_directories` for the parent (nested release paths);
  strict improvement, existing callers unaffected, works on native + Emscripten MEMFS.
- **Verbs** (all headless): `--project-publish <path> [channel]` (idempotent; re-publish =
  *verified*; refuses to overwrite a release id with different bytes), `--release-promote
  <from> <to>`, `--release-rollback <channel> <id>`, `--release-status` (reads the two
  fixed channel files — **no directory scan**, per the "collection database" warning).
- **Tests** `tests/test_release.cpp` (paths, channel round-trip + fail-closed, validators);
  CI gained a publish→status→promote lifecycle smoke. Suite 50/50.

**Exit:** the content-flow vertical is complete end to end — create project → declare
content → validate closure → package (fingerprint) → **publish immutably → promote /
rollback by moving a pointer** — native + web + CI.

### Deferred from Horizon 1 (each with a trigger)
- **Release log / history listing** — append-only file when a UI/audit needs to enumerate
  past releases; never a directory scan.
- **Preview-parity diff (metric P2)** — compare a running preview to a published release
  by hash; hashes are in place, the comparison is a later slice.
- **Server-side release hosting** — same path scheme behind HTTP/object store; ops-heavy,
  stays **conditional on evidence** per the blend posture, not scheduled.
- **Input-action mapping** (was H0→H1 slice) — still deferred: one fixed-binding game has
  no second consumer; revisit when a rebind UI or second scheme needs it.

## What "the rest of the plan" means now (honest scope)
Horizons 2–4 are, by the adopted blend posture, **ops-heavy and conditional on evidence**
(Postgres/RBAC/audit/backup/SLO/LiveOps = H2; external beta = H3; curated catalog = H4).
The strategy says *integrate commodity infra, don't hand-build it* — and the engine's
non-negotiable constraint is *SDL2 is the only runtime dependency*. So those horizons are
**not** hand-built into the engine on momentum; they wait on real evidence (users,
traffic) and would live in the separate `baas/` process. The buildable spine the strategy
asked to finish *first* — Horizon 0 + Horizon 1 — is now complete.
