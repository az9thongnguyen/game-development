# Chapter 82 — The Sandbox Test-Runner: Two Halves by Necessity

> Code: `baas/test_runner/testrun_{service,controller}.{h,cc}` (coordinator),
> `src/games/runner/runner.{hpp,cpp}` (scenario exec) + `worker.{hpp,cpp}` (the worker),
> `src/main.cpp` (`--runner`), `sdk/cpp` (`Client::TestRuns`);
> tests `tests/test_baas_testruns.cc`, `tests/test_runner.cpp`, `tests/test_sdk_live.cc`.

"Managed test-runs" sounds like one feature. It is two — and *which* two is dictated by a
rule you cannot bend: **`baas/` links no engine code** (CLAUDE.md). The BaaS cannot run a game.
So the feature splits along exactly that line:

- a **coordinator** in the BaaS — a job queue that stores runs and their results but executes
  nothing;
- a **worker** on the engine side — `demo --runner` — that pulls jobs, *runs* them, and reports
  back.

The architecture constraint is not an obstacle here; it is the design. This chapter is about
letting a hard rule shape a feature into the right shape.

## 1. The coordinator: a queue that runs nothing

`/v1/testruns` is the asset registry's twin — project-scoped, api-key gated, same service +
controller shape — with one addition: a **status lifecycle**.

```
   POST /v1/testruns          create           → pending
   POST /v1/testruns/{id}/claim   worker grabs  → running   (atomic)
   PATCH /v1/testruns/{id}    worker reports    → passed | failed | error
```

The two transitions that matter are **claim** and **complete**, and both are *conditional
UPDATEs* so the state machine can't be corrupted by concurrency:

```cpp
// claim: only a pending run becomes running — two workers can't both win it
"UPDATE testruns SET status='running' WHERE project_id=? AND id=? AND status='pending'"
    → affectedRows() > 0

// complete: only a running run can finish
"UPDATE testruns SET status=?, result=? WHERE project_id=? AND id=? AND status='running'"
```

The database, not application logic, enforces "claimed at most once" — the `WHERE status=…`
clause is the lock. The test proves it: a second `claim` on the same run returns **409**, and
completing a run that isn't running returns **409**. No mutex, no transaction ceremony — the
row's own status is the guard.

The coordinator stores `scenario` and `params` as opaque text and never looks inside them.
It could not act on them if it wanted to; it has no engine to do so. That ignorance is the
point.

## 2. The scenario is a sandbox scene (reuse, again)

What is a "run"? Rather than invent a scenario language, a scenario **is a `sandbox1` scene** —
the exact save format from ch.76 — plus `params`:

```
scenario:  sandbox1\nbounds 936 560\ne x=100 y=100 …          (a saved sandbox)
params:    steps=10;expect_alive=1
```

`run_scenario` (pure, `runner_core`, no SDK/no Drogon) loads it with `sandbox::from_scene`,
ticks `World::tick` `steps` times deterministically, and passes iff `alive() == expect_alive`:

```cpp
sandbox::World w = sandbox::from_scene(scenario);
for (long i = 0; i < steps; ++i) w.tick(1.0f / 60.0f);
return { w.alive() == *expect ? "passed" : "failed", summary };
```

Because the sim is deterministic (ch.76), the check is meaningful: the same scenario always
gives the same verdict. A malformed scene or missing `expect_alive` → `error`, never a false
pass. `test_runner` covers all four outcomes headless — including a lifetime-despawn scenario
that *passes* by ending with zero actors. The whole test-runner rides on infrastructure slice
1 already built and proved.

## 3. The worker: where both halves are linked

`process_one` is the only place the SDK and the engine meet:

```cpp
bool process_one(gbaas::Client& c) {
    // list pending → claim one → run_scenario (ENGINE) → complete (SDK)
}
```

It links `gbaas_sdk` (to talk to the coordinator) *and* `runner_core` (to execute) — a
combination the BaaS process is forbidden from making. `demo --runner <url> <key>` is a bare
loop over `process_one` with a back-off when idle: a headless daemon, not a windowed scene, so
it bypasses `platform::init` entirely. It is a *tool*, like the server, not a game frame loop —
which is why a plain `for(;;)` is allowed here where it would be forbidden above the platform
layer.

`process_one` is deliberately synchronous (it pumps the non-blocking client internally until
each step lands), so it is one testable unit: the full-loop test in `test_sdk_live` submits a
scenario through the SDK, calls `process_one` against the *same live server*, and asserts the
run ends `passed` — the entire coordinator→worker→result cycle, over real HTTP, in one test.

## 4. The whole picture

```
   operator ──submit(scenario,params)──►  BaaS /v1/testruns  ── testruns table ──► pending
                                                  ▲   │
                                     claim/complete│   │ list ?status=pending
                                                  │   ▼
                                   demo --runner  (worker)
                                     ├─ SDK: claim the run
                                     ├─ runner_core: run_scenario (sandbox sim)  ← ENGINE
                                     └─ SDK: complete with passed/failed/error
                                                  │
   operator ──get(id)──►  BaaS  ◄────────────────┘  status: passed
```

Two processes, one job, joined by an HTTP queue — because the engine may run games and the
BaaS may not, and the feature is exactly the seam between them.

## Pitfalls

- **Running the scenario in the BaaS.** Forbidden — it links no engine. The split is not
  optional.
- **Claiming without a conditional UPDATE.** Two workers double-run the job. Let the `WHERE
  status='pending'` be the lock.
- **A non-deterministic scenario.** Then "passed" means nothing. The sandbox sim is
  deterministic on purpose (ch.76); keep it that way.
- **Unbounded `steps`.** A huge `steps` hangs the worker. `run_scenario` caps it (100k).

## Glossary

- **coordinator** — the BaaS job queue; stores runs, executes nothing.
- **worker** — `demo --runner`; links engine + SDK, runs jobs, reports results.
- **claim / complete** — the two conditional-UPDATE transitions that make the state machine
  concurrency-safe.
- **scenario** — a `sandbox1` scene the worker runs deterministically.

## Exercises

1. **Timeout.** A run claimed but never completed sits `running` forever. Add a server-side
   sweep that returns stale `running` runs to `pending`. What column do you need?
2. **Richer verdicts.** Extend `params` to assert on a specific actor's position, not just the
   count. Where does the parse + check live — coordinator or `runner_core`?
3. **Asset-backed scenarios.** Let a scenario reference an asset by name (ch.81) instead of
   inlining the scene text. Which service does the worker call first?
4. **Parallel workers.** Run two `--runner` processes against one coordinator. What already
   makes this safe, and what (fairness, starvation) does it not yet handle?
