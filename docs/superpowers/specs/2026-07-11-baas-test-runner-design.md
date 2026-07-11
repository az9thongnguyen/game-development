# BaaS Sandbox Test-Runner — Design Spec

**Date:** 2026-07-11 · **Track:** C (lean BaaS) — sub-project 2 · **Status:** approved (user picked "full loop").

## 1. Goal

Managed headless test-runs ("sandbox / quản lý chạy thử"): submit a **scenario** to the BaaS, a
**worker** picks it up, runs it headlessly, and posts a pass/fail **result** you can poll. Two parts,
because the architecture demands it:

- **Coordinator (BaaS):** a `/v1/testruns` job registry — create / list / poll / claim / complete. Pure
  coordination; stores no engine logic. Mirrors the asset registry module.
- **Worker (engine side):** `demo --runner <url> <key>` polls the coordinator, claims a pending run,
  **executes the scenario deterministically**, and posts the result. It links the engine + SDK — which
  the BaaS may never do (CLAUDE.md: `baas/` links no engine code). That rule is *why* there are two parts.

## 2. What a "scenario" is (reuse, don't invent)

A scenario is a **sandbox scene** in the existing `sandbox1` text format (slice 1's `to_scene`/
`from_scene`), plus params. The worker loads it, runs `World::tick` for N fixed steps (deterministic),
and checks an invariant. So the test-runner rides on the deterministic sim already built — a scenario
*is* a saved sandbox, and "did it behave" is `alive()` after N steps.

- `scenario` (TEXT): the `sandbox1` scene text.
- `params` (TEXT): a tiny `key=value;…` string — `steps=<N>;expect_alive=<K>`. (Not JSON: the pure
  runner core parses it without a JSON dep.)
- **Pass** iff, after `steps` ticks, `world.alive() == expect_alive`. Else **fail**. A malformed
  scenario/params → **error**.

## 3. Coordinator — `web::testrun` + `TestRunController`

Table (added to `baas/db/db.cc`):

```sql
CREATE TABLE IF NOT EXISTS testruns (
  id INTEGER PRIMARY KEY,
  project_id INTEGER NOT NULL REFERENCES projects(id),
  scenario TEXT NOT NULL,
  params TEXT NOT NULL DEFAULT '',
  status TEXT NOT NULL DEFAULT 'pending',   -- pending|running|passed|failed|error
  result TEXT NOT NULL DEFAULT '',
  created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

Service `web::testrun` (project-scoped, pure DB logic):

```cpp
long              create(long project_id, const std::string& scenario, const std::string& params);
std::optional<Record> get(long project_id, long id);
std::vector<Record>   list(long project_id, const std::string& status_filter);   // "" = all
bool              claim(long project_id, long id);                    // pending -> running (atomic)
bool              complete(long project_id, long id, const std::string& status, const std::string& result);
bool              valid_status(const std::string&);                  // passed|failed|error only
```

`claim` is `UPDATE … SET status='running' WHERE …=id AND status='pending'` → `affectedRows()>0`, so two
workers can't grab the same run. `complete` is `… WHERE …=id AND status='running'` → only a claimed run
finishes.

Controller (api-key gate only — runs belong to the project/operator, not a player):

- `POST   /v1/testruns`         `{scenario, params}` → `{id, status:"pending"}`
- `GET    /v1/testruns`         `?status=` filter → `{"testruns":[Record…]}`
- `GET    /v1/testruns/{id}`    → full Record (404 if absent)
- `POST   /v1/testruns/{id}/claim`   → 200 Record if claimed, 409 if not pending, 404 if absent
- `PATCH  /v1/testruns/{id}`    `{status, result}` → 200 if completed, 409 if not running

Scenario cap 256 KiB. `> deferred:` a dedicated worker credential (v1 uses the project api key — fine
for a single operator).

## 4. Worker

- **`runner_core`** (`src/games/runner/runner.{hpp,cpp}`, engine side, no SDK/no Drogon):
  `RunOutcome run_scenario(const std::string& scenario, const std::string& params)` →
  `{status ("passed"/"failed"/"error"), result (a short human string)}`. Pure → unit-tested headless.
  Uses `sandbox::from_scene` + `World::tick`. Parses `steps`/`expect_alive` from params.
- **`process_one(gbaas::Client&)`** (demo side): list `?status=pending`, claim the first, run it via
  `run_scenario`, `PATCH` the result. Returns whether it did work. Testable against a live server.
- **`demo --runner <base_url> <api_key>`**: a headless loop (no window) calling `process_one` until
  idle then sleeping. The one place both the engine and the SDK are linked.

## 5. SDK — `Client::TestRuns`

```cpp
struct TestRun { long long id; std::string scenario, params, status, result; };
class TestRuns {
  void submit(const std::string& scenario, const std::string& params, cb<TestRun-ish {id,status}>);
  void get(long long id, cb<TestRun>);
  void list(cb<std::vector<TestRun>>);          // metas
  bool/void claim(long long id, cb<TestRun>);    // worker
  void complete(long long id, const std::string& status, const std::string& result, cb<bool>);  // worker
};
```

Built on `Client::request<T>`, api-key auto-attached.

## 6. Tests

- **`tests/test_baas_testruns.cc`** (integration): create→pending; list + `?status=` filter; get; claim
  (pending→running, second claim→409); complete (running→passed, result stored); complete a non-running→409;
  cross-tenant isolation; validation (bad status→400, oversized scenario→413).
- **`tests/test_runner.cc`** (headless, `runner_core`): a passing scenario (spawn 1 static actor,
  `steps=10;expect_alive=1` → passed); a failing one (`expect_alive=0` → failed); a lifetime scenario that
  despawns (an actor with a short `lifetime`, `steps` past ttl, `expect_alive=0` → passed); malformed
  scenario/params → error.
- **`tests/test_sdk_live.cc`** (append): submit a run through the SDK, drive `runner::process_one` against
  the live server, then poll `get` and assert `status=="passed"` — the **full loop** end-to-end.

## 7. Files

- Modify: `baas/db/db.cc` (testruns table), `baas/CMakeLists.txt` (module + test), `src/main.cpp`
  (`--runner`), root `CMakeLists.txt` (`runner_core`, `test_runner`, worker src on demo), SDK
  `client.h`/`client.cc`, `tests/test_sdk_live.cc`
- Create: `baas/test_runner/testrun_service.{h,cc}` + `testrun_controller.{h,cc}`,
  `src/games/runner/runner.{hpp,cpp}`, `src/games/runner/worker.{hpp,cpp}` (process_one),
  `tests/test_baas_testruns.cc`, `tests/test_runner.cc`
- Docs: guidebook `docs/book/82-baas-test-runner.md`, README

## 8. Non-goals (deferred)

Worker credentials/queue fairness, parallel workers, run cancellation/timeout enforcement server-side,
richer scenarios (fps/iso, asset refs), ret/logs streaming, a dashboard runs view.
