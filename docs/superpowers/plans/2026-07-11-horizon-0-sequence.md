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

### Horizon 1 — release-ops hardening (DONE, `docs/book/94`, exit gates 3/5/7)
Closed the three ways the naïve store was not production-shaped, per the roadmap's own
"stabilize commands before UI" risk note:
- **Gate 3 (atomic):** `write_atomic` (stage `.tmp` → `assets::rename`) for the release
  manifest and channel pointers — a crash never exposes a torn/partial release; pointer
  is written last so a half-publish leaves an unreferenced immutable release, not a
  dangling channel. New seam primitive `assets::rename`. CI asserts zero `.tmp` litter.
- **Gate 5 (audited):** append-only `releases/audit.log` — every publish/promote/rollback
  records `epoch action channel release <prev|-> reason`. Pure `audit_line`/`parse_audit_line`
  (fail-closed, reason-last-so-it-can-have-spaces). `--release-log [channel]` reads it
  *forward* (no directory scan). New seam primitive `assets::append_file`.
- **Gate 7 (predecessor):** each move records the id it displaced; because releases are
  immutable that predecessor is still in the store → a bad release always has a known-good
  rollback target. Proven in the smoke (publish v1 → bad v2 → rollback to v1, v1 present).
- **Channel semantics:** development → preview → production; publish defaults to
  `development` (publish low, promote up); promotion copies a pointer (bit-identical).
- `tests/test_release.cpp` extended (audit round-trip + fail-closed); suite 50/50.

### Horizon 1 — Hub shell v1 (DONE, `docs/book/95`)
The first Hub shell as a headless view/controller — built *after* the release domain was
stabilized (the roadmap's precondition), so the UI logic sits on solid ground:
- **`hub_core::recommend(HubView)`** — PURE decision brain: from a project's aggregate
  state, the single next action (fix → publish → promote-preview → promote-production →
  in-sync). `matches_local` guards stale bytes (published id must equal the current
  source's package hash). Unit-tested with hand-built `HubView`s — no filesystem, no window.
- **`build_hub_view` + `--hub <path>`** (main.cpp) — the I/O assembly + a headless
  dashboard: shippable?/problems, source package hash, each channel's pointer + `==source`.
- One reference project, NOT a directory-scan browser (collection-db discipline). The
  graphical Scene is deferred to the unblocked pixel path — the decision is the tested part.
- `tests/test_hub.cpp` (`hub` CTest); suite **51/51**; CI runs `--hub`.

### Horizon 1 — graphical Hub Scene (DONE, `docs/book/96`)
`--hub-ui [path]` renders the hub in a window, sharing ONE pure `hub_core::hub_lines(HubView)`
with the headless `--hub` (CLI and window cannot drift — the roadmap's "Hub and CI surface
identical diagnostics", satisfied by construction). Three layers, each tested at its level:
pure decision/display (`hub.cpp`, unit test), impure assembly (`hub_build_core`, CI smoke),
thin render (`HubScene`, manual visual accept). Read-only for now (shows the recommended
verb; you run it via CLI, press R to refresh). Suite 51/51.

**Remaining Horizon 1 (honest boundary):** the **full Studio shell** (docked panels,
thumbnails, and *interactive* mutation — which needs the domain ops extracted from main.cpp
behind a callable interface; a real subsystem deserving its own brainstorming pass), a
**hosted artifact adapter** (evidence-gated ops; the local adapter proves the mechanics),
and **BaaS HTTP-contract conformance** (lives in `baas/`, not the engine). Two pixel-level
manual accepts stand open: web `?mode=project` and `--hub-ui` (both need eyes on a window).

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
