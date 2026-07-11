# BaaS Sandbox Test-Runner — Implementation Plan

> One behaviour per commit. Coordinator mirrors the asset registry; worker reuses the sandbox sim.

**Goal:** `/v1/testruns` job registry (BaaS) + a `demo --runner` worker that runs sandbox scenarios headlessly and posts pass/fail. A "scenario" = a `sandbox1` scene + `steps=N;expect_alive=K`.

---

### Task 1: coordinator — table + service + controller + test
- `baas/db/db.cc`: add the `testruns` table (spec §3).
- `baas/test_runner/testrun_service.{h,cc}` (`web::testrun`): `create/get/list/claim/complete/valid_status`
  — mirror `asset_service` shape; `claim` = UPDATE…WHERE status='pending' (affectedRows>0); `complete` =
  UPDATE…WHERE status='running'.
- `baas/test_runner/testrun_controller.{h,cc}` (`TestRunController`, api-key gate): POST/GET/GET{id}/
  POST{id}/claim/PATCH{id}. Scenario cap 256 KiB; bad complete status → 400.
- `baas/CMakeLists.txt`: 2 sources → `baas_core`; `test_baas_testruns` target.
- `tests/test_baas_testruns.cc`: create→pending, list+filter, get, claim (2nd→409), complete (non-running→409),
  cross-tenant, validation. Build; `ctest -R baas_testruns` → PASS. Commit.

### Task 2: runner_core — pure scenario execution
- `src/games/runner/runner.{hpp,cpp}` (`runner_core`): `struct RunOutcome{std::string status,result;}`;
  `RunOutcome run_scenario(const std::string& scenario, const std::string& params)`:
  parse `steps`/`expect_alive` from `k=v;` params (hand parse); `sandbox::World w = sandbox::from_scene(scenario);`
  guard empty; `for(steps) w.tick(1/60);` compare `w.alive()`==expect → passed/failed; catch → error.
- root `CMakeLists.txt`: `runner_core` STATIC (`runner.cpp`, link `sandbox_core engine_flags`); `test_runner`
  (compile `runner.cpp`+`world.cpp`+`serialize.cpp`+`engine/ecs/registry.cpp` directly, dependency-free like
  test_sandbox); `add_test(NAME runner …)`.
- `tests/test_runner.cc`: passing (1 static actor, expect_alive=1), failing (expect_alive=0), lifetime despawn
  (short-ttl actor, steps past ttl, expect_alive=0 → passed), malformed → error. Build; `ctest -R runner`. Commit.

### Task 3: SDK TestRuns handle
- `client.h`: `struct TestRun{long long id; std::string scenario,params,status,result;}`; `class TestRuns
  {submit,get,list,claim,complete}`; `TestRuns testruns(){…}`.
- `client.cc`: implement via `request<T>`. submit body `{scenario,params}`; complete body `{status,result}`
  PATCH. list parses `{"testruns":[…]}`.
- Build (SDK compiles into demo/tests). Commit.

### Task 4: worker + `--runner` + full-loop test
- `src/games/runner/worker.{hpp,cpp}` (demo side): `bool process_one(gbaas::Client& c)` — pump-driven
  (internal `pump` like the tests): list `?status=pending`; if none return false; claim first; `run_scenario`;
  complete with the outcome; return true.
- `src/main.cpp`: `--runner <base_url> <api_key>` → construct `gbaas::Client`, loop `process_one` then a short
  sleep when idle (headless tool loop; no window). Include worker.hpp.
- root `CMakeLists.txt`: add `src/games/runner/worker.cpp` to demo sources; demo already links gbaas_sdk +
  runner_core + sandbox_core.
- `tests/test_sdk_live.cc`: submit a passing scenario via SDK → call `runner::process_one(c)` (links worker +
  runner_core) → poll `get(id)` → assert `status=="passed"`. Add worker.cpp + runner.cpp to `test_sdk_live`
  sources (it already boots a live server). Build; `ctest -R sdk_live` → PASS. Commit.

### Task 5: docs + merge
- Guidebook `docs/book/82-baas-test-runner.md` (why two parts = the no-engine-in-baas rule; scenario=sandbox
  scene reuse; claim/complete atomicity; the worker; the full loop). README row.
- Merge `--no-ff`; delete branch; checkpoint memory.

## Self-review
- The no-engine-in-baas rule is the design driver: coordinator (baas) vs worker (engine+SDK). Scenario reuses
  `sandbox::from_scene`+`tick` (slice 1). `claim`/`complete` atomic via conditional UPDATE. Types (`testrun::Record`,
  SDK `TestRun`, `RunOutcome`) consistent across service/controller/SDK/runner/tests.
