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

### Horizon 1 — interactive Hub + Studio shell (DONE, `docs/book/97`)
- **`release_ops_core`**: `publish`/`promote`/`rollback` extracted from main.cpp, returning
  `OpResult{ok,message}` — called by both the CLI (thin wrappers) and the Scenes. **Now
  unit-tested end-to-end** (`test_release_ops`, temp asset base) → the ops are verified;
  only keypress→op wiring is manual.
- **Interactive `HubScene`** (`--hub-ui`): Space=publish→dev, 1/2=promote, R=refresh, flashes
  the result. **Studio shell** (`--shell`): nav rail Hub/Learn/About; Hub section reuses
  `hub_lines` + ops (no dup), Learn = doc map, About = project info.
- **Web modes** `?mode=hubui` / `?mode=shell` in shell.html; wasm rebuilt. Suite 52/52.

**Remaining Horizon 1 (honest boundary):** a **hosted artifact adapter** (evidence-gated
ops; the local adapter proves the mechanics) and **BaaS HTTP-contract conformance** (lives
in `baas/`, not the engine). The one open non-code item: the **browser pixel accept** —
`?mode=project` / `?mode=hubui` / `?mode=shell` + native `--hub-ui`/`--shell` — blocked only
on the Claude Chrome extension being connected (all built + build-verified, awaiting eyes).

### Deferred from Horizon 1 (each with a trigger)
- **Release log / history listing** — append-only file when a UI/audit needs to enumerate
  past releases; never a directory scan.
- **Preview-parity diff (metric P2)** — compare a running preview to a published release
  by hash; hashes are in place, the comparison is a later slice.
- **Server-side release hosting** — same path scheme behind HTTP/object store; ops-heavy,
  stays **conditional on evidence** per the blend posture, not scheduled.
- **Input-action mapping** (was H0→H1 slice) — still deferred: one fixed-binding game has
  no second consumer; revisit when a rebind UI or second scheme needs it.

### Horizon 2 — production persistence + failure drill (DONE, `docs/book/98`)
The first H2 slice, built where H2 *belongs* — the separate `baas/` process, which links
no engine code (so the SDL2-only engine constraint is untouched):
- **Versioned migration engine** (`baas/db/db.cc`): a `schema_migrations` ledger + an
  ordered, append-only `kMigrations` list, replacing the single embedded blob the code
  itself flagged for replacement. Applies only what a DB is behind on; idempotent;
  backward-safe on pre-versioning databases (migration 1 is the old `IF NOT EXISTS` schema).
- **Migration 2 = `audit_log`** — proves the engine *and* delivers the RBAC/audit
  foundation. `web::audit::record`/`recent` ("who changed what, when"); wired into one real
  call site (`admin::create_project`). Audit failure never breaks the decorated action.
- **Backup/restore drill** (`baas/ops/backup_restore_drill.sh`): back up → simulate total
  loss → restore → verify the restored `.dump` SHA-1 equals the original + `integrity_check`.
  **Verified passing** — this is the H2 exit gate ("survives a documented failure drill")
  satisfied for the persistence layer's SQLite runtime, executed not asserted.
- **Test** `test_baas_migrations` (pure DB, no HTTP): ordered+recorded, idempotent, audit
  round-trip. Full baas suite **21/21** (every test exercises the changed `run_migrations`).
- Runbook: `docs/guides/backup-restore-drill.md`.

### Horizon 2 — reversible LiveOps change (DONE, `docs/book/99`)
Closes the H2 exit gate's third clause ("runs a reversible LiveOps change without client
redeployment"), reusing the audit log from the persistence slice (compose, don't build):
- **`cfg::set_audited` / `remove_audited`** (`baas/remote_config/config_service.cc`): upsert a
  remote-config value, record the old→new transition in `audit_log`, and **return the prior
  value** — which *is* the reversibility (the operator sets it back to revert). Clients keep
  reading `/v1/config` unchanged, so the change lands with no redeployment.
- Operator endpoints (`PUT/DELETE /v1/admin/config/{key}`) now return `"previous"` so a
  dashboard can offer one-click revert.
- **Test** `test_baas_liveops` (pure DB): new-set → change → audit shows old→new → revert →
  audited delete. `baas_config`/`baas_admin` HTTP tests still green. No new table, no config
  history subsystem (YAGNI — the audit row answers "what was it just before").

### Horizon 2 — measuring a release (DONE, `docs/book/100`)
Closes the H2 exit gate's second clause ("measures a release") — the last one:
- **Migration 3** (`ALTER TABLE analytics_events ADD COLUMN release`): the engine's first
  *alter* (not a `CREATE IF NOT EXISTS`), the operation a versioned engine actually exists for
  — run twice it would error "duplicate column"; the ledger applies it exactly once. Portable
  (sqlite+pg), backfills existing rows with `''`.
- **`analytics::record(..., release="")` + `summary_by_release`** (`GROUP BY release, name`):
  events carry the client's release id; the admin summary compares one release vs another.
  Controller reads an optional capped `"release"` field (bound param, injection-safe).
- **Test** `test_baas_release_metrics` (pure DB): release A (5 sessions/2 errors) vs release B
  (3 sessions/9 errors) — asserts B's error spike is attributed + visible, the exact signal
  "measure a release" exists for. `baas_analytics` HTTP path still green.

**H2 exit gate — ALL THREE CLAUSES MET for the SQLite runtime** (executed, not asserted):
survives a failure drill (ch.98) · measures a release (ch.100) · reversible LiveOps change
without client redeployment (ch.99). Full baas suite **23/23**.

### Horizon 2 — secret rotation (DONE, `docs/book/101`) [hardening beyond the exit gate]
The "secret rotation" half of the RBAC/audit item: `admin::rotate_secret(pid)` mints a fresh
project secret, swaps the stored argon2id hash, audits `secret.rotate`, returns the new secret
once. Endpoint `POST /v1/admin/secret/rotate` behind ApiKeyFilter+SecretKeyFilter — reachable
only by a caller proving it holds the *current* secret (self-service rotation, not admin
override). Old secret dies the instant the UPDATE lands. Test `test_baas_secret_rotation` (pure
DB): new verifies, **old no longer verifies**, rotation audited. Still open in RBAC (named, not
faked): multi-operator roles (owner/admin/viewer, needs an operators table) + short-lived
credentials (token issuance design).

### Horizon 2 — idempotency keys (DONE, `docs/book/102`) [correctness hardening]
The "idempotency" half of economy foundations, on the op where it bites hardest (grant):
- **Migration 4** `idempotency_keys(project_id, idem_key, result, UNIQUE(project_id,idem_key))`.
- `inv::grant(...)` gains an optional `idem_key`: a retried grant with the same key **replays**
  the first result instead of crediting again (no double-grant on a network retry). Records
  with `ON CONFLICT DO NOTHING` (portable); only records *successes* (a rejected grant leaves
  the key free). Controller reads the `Idempotency-Key` header (capped 64).
- **Test** `test_baas_idempotency` (pure DB): same key → not double-credited; different key →
  applies; no key → applies; bad-amount key not poisoned; **cross-user + cross-item reuse of one
  key value each get their own grant** (regression for the review fix below). `baas_inventory` green.
- ponytail ceiling named: lookup-then-record has a thin concurrent-first-use window, closed by
  single-writer SQLite today; claim-first INSERT is the Postgres upgrade.
- **Review fix (cpp-reviewer, HIGH):** the client key is scoped to `(user_id, item, idem_key)`
  (composed as `"<uid>|<item>|<key>"`) — the first cut keyed on `(project_id, idem_key)` only,
  which would silently replay the wrong user's/item's grant when a client key value collided.
- **Review note (MEDIUM, documented):** migration statements aren't transaction-wrapped; a
  `ponytail:` invariant on `run_migrations` requires each new migration to be single-statement OR
  fully idempotent (migrations 1-4 satisfy it), with `db->newTransaction()` as the upgrade.

### Horizon 2 — atomic purchase (DONE, `docs/book/103`) [economy foundations]
The "atomic transactions" half of economy foundations, the sibling of idempotency:
- `inv::purchase(pid, uid, currency, cost, item, amount, idem_key="")`: spends a currency and
  grants an item in ONE Drogon transaction (`db->newTransaction()`; commit on scope exit, else
  `rollback()`). Affordability check + spend are atomic against other requests (one reserved
  connection); insufficient funds → pure no-op; idempotency record written INSIDE the txn so key
  + effects commit together. Key scoped `"purchase|<uid>|<item>|<key>"` (no collision with grant).
- **Test** `test_baas_purchase` (pure DB): atomic success; **insufficient → rollback, balance
  unchanged** (proves the txn really rolls back, not just the happy path); idempotent retry not
  double-charged; different key = new purchase; bad amount rejected pre-spend.
- Economy still ahead (named, not faked): currency/catalog model (priced items vs caller-supplied
  cost), receipt validation (platform-integration, not hand-built) — come when a ref game sells.

### Horizon 2 — store catalog (DONE, `docs/book/105`) [economy — menu item C]
Priced offers the server owns, so the client buys a SKU not a caller-supplied cost:
- **Migration 5** `catalog(project_id, sku, currency, cost, item, amount, UNIQUE(pid,sku))`.
- `store::Offer/get/list/upsert` (upsert audited `catalog.upsert`, rejects non-positive
  cost/amount); **`store::buy`** resolves the SKU → delegates to the atomic idempotent
  `inv::purchase` (ch.103) — inherits transaction/rollback/idempotency, adds only the price
  lookup + unknown-SKU 404. Reuse-before-write in its purest form.
- Endpoints: admin `PUT /v1/admin/catalog/{sku}` (secret-gated, defineOffer); client
  `GET /v1/store/catalog` (api-key) + `POST /v1/store/buy/{sku}` (api-key+JWT, forwards
  Idempotency-Key). New `StoreController` (self-registering).
- **Test** `test_baas_catalog` (pure DB): define offer; buy spends/grants catalog amounts;
  zero-cost/bad-sku rejected; unknown SKU 404 no spend; unaffordable → inherited rollback;
  idempotent retry; **re-price then buy uses new price** (server owns price = a LiveOps lever).
- Economy still ahead: real-money receipt validation (platform-integration, when a ref game sells).

### Horizon 2 — request correlation ids (DONE, `docs/book/104`) [telemetry — menu item D]
Every response now carries an `X-Request-Id` (adopt an inbound one for cross-proxy tracing, or
mint 16 random hex). A pre-routing advice (registered before rate-limit, so even a 429 has an id)
sanitizes any client-supplied id ([A-Za-z0-9_-], cap 64 — prevents log-forging) and stores it;
the pre-sending advice echoes it as a header and prefixes the access-log line `[rid] METHOD path
status ms`. **Test** `test_baas_tracing` (boots a server): minted id is 16-hex; two mints differ;
inbound id echoed verbatim; hostile id sanitized. `baas_test_util.h` gained additive response-
header capture (`raw_headers` + `header_value`) reused for this. Full baas suite **27/27**.

**Deployment profile (Dockerfile) — deferred with reason:** root CMake `pkg_check_modules(SDL2
REQUIRED)` at *configure* time couples the whole project to SDL2, so a backend-only image would
awkwardly need SDL2 just to configure `baas`. That deserves a build-system split (make SDL2
optional when only baas targets are requested) FIRST — a broad, risky change not done on
momentum. Docker IS installed (29.6.1); the slice is real but gated on that refactor.

**H2 honest boundary (toolchain-checked, not assumed):** the persistence *mechanism* is
buildable and tested here because the toolchain is present (Drogon + SQLite + libsodium).
**Live PostgreSQL is the one genuine block**: the Homebrew Drogon bottle is built without
libpq (`otool -L` → only `libsqlite3`), so `newPgClient` has no backend. The seam, portable
migration SQL, and the drill's `pg_dump` branch are all in place — moving to Postgres is a
*deploy-side Drogon rebuild + `db_url` change*, no app code — but a live-Postgres acceptance
run belongs in a deploy environment and is **not claimed**. Rebuilding Drogon from source is
a system-toolchain change, deliberately not done autonomously on momentum.

## What "the rest of H2–H4" means now (honest scope)
The user directed "finish H2–H4." H2 is a *multi-slice* horizon; this delivered its
first + exit-gate-relevant slice (persistence + failure drill + audit foundation). Still
open in H2 and buildable in `baas/` when tackled: the rest of RBAC (roles, short-lived
credentials, secret rotation), full telemetry (traces/correlation IDs/SLOs), LiveOps
segmentation/experiments, economy foundations (catalog/currencies/atomic txns/idempotency),
deployment profiles, and a second online reference game.

**H3/H4 cannot be "code-completed"** — the strategy the user approved gates them on external
*humans and evidence*: H3's exit is "three external teams complete the golden path without
author intervention"; H4's is "evidence of repeat creators, repeat players, acceptable
moderation load." No amount of writing code satisfies those gates; they need an external
beta and a running ecosystem. Writing speculative catalog/moderation/payout code now would
violate the strategy's own §15 non-goals and principle 7 ("production readiness is a
*measurable* claim"). So the honest maximum is: keep shipping buildable, tested H2 slices in
`baas/`, and surface H3/H4 as evidence-gated rather than fake their completion.
