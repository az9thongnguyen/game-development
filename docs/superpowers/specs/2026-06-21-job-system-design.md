# Subsystem C — Job System — Design Spec

> Date: 2026-06-21 · Program A→F, step C · Branch `feat/jobs`
> Resolves the threading posture deferred from A. Builds on nothing required (uses
> std::thread); pairs naturally with B (parallelize systems) and E (physics).

## 1. Goal & the threading decision

A **job system** runs work in parallel across CPU cores: submit small tasks, run a
`parallel_for`, wait for completion. The engine's standing invariant is
*single-threaded, web-first* (requirements §2/§9). We reconcile with a **pluggable
executor behind one API**:

- **Desktop:** a real **thread pool** (N = `hardware_concurrency()-1` workers).
- **Web (`__EMSCRIPTEN__`, or 0 workers):** **synchronous** — `run()` executes the job
  immediately on the caller; `wait()` is a no-op. So the web build stays a drop-in
  (no pthreads, no COOP/COEP headers), exactly as before.

Parallelism becomes an *opt-in* primitive used inside systems; the loop above the
platform is unchanged. Alternative considered: a purely cooperative single-threaded
scheduler — rejected because it teaches task graphs but gives no real speedup.

## 2. Model: counters + wait-and-help

Borrowed from modern engines (e.g. Naughty Dog's fiber jobs, minus fibers):

- A **Counter** is an atomic pending-job count.
- `run(counter, fn)` increments the counter and enqueues a task that runs `fn` then
  decrements + notifies.
- `wait(counter)` blocks until the counter hits 0 — but **while waiting it pops and
  runs queued jobs itself** ("help"), so no core sits idle and a job that waits on
  child jobs can't deadlock the pool.
- `parallel_for(n, fn, grain)` splits `[0,n)` into chunks of `grain`, `run`s each
  against a local counter, then `wait`s — the common data-parallel case.

Simple submit (fire-and-forget against an internal counter) and `wait_idle()` (drain
everything) are provided too.

## 3. Concurrency primitives (the teaching content)

- A **thread-safe queue** of `std::function<void()>` guarded by a `std::mutex` +
  `std::condition_variable` (workers sleep when idle, wake on new work).
- `std::atomic<int>` counters; `std::atomic<bool>` shutdown flag.
- Workers join in the destructor (clean shutdown, no detached threads).
- Mutex-protected queue (correct + simple), **not** lock-free work-stealing — that is
  a documented exercise. Correctness over cleverness.

## 4. Files & build

```
src/engine/jobs/job_system.hpp / .cpp   JobSystem + Counter + run/wait/parallel_for/
                                         wait_idle; #ifdef __EMSCRIPTEN__ synchronous path
tests/test_jobs.cpp                       CTest 'jobs'
docs/book/41,42 (split)
```

CMake: `jobs_core` static lib; link `Threads::Threads` (find_package(Threads)) on
native, nothing on Emscripten. `test_jobs` links it. A dedicated **ThreadSanitizer**
build of `test_jobs` validates there are no data races (TSan can't combine with ASan,
so it's a separate one-off check, not the default ENGINE_SANITIZE).

## 5. API sketch

```cpp
namespace jobs {
struct Counter { std::atomic<int> pending{0}; };

class JobSystem {
public:
    explicit JobSystem(int worker_count = -1);   // -1 => hardware_concurrency()-1; 0 => synchronous
    ~JobSystem();                                 // joins workers

    void run(Counter& c, std::function<void()> fn);   // counted task
    void run(std::function<void()> fn);               // fire-and-forget (internal counter)
    void wait(Counter& c);                            // block (and help) until c == 0
    void wait_idle();                                 // drain all queued work
    void parallel_for(std::size_t n, const std::function<void(std::size_t)>& body,
                      std::size_t grain = 1);

    int worker_count() const;
};
} // namespace jobs
```

## 6. Correctness focus
- No data races (TSan-clean): queue access only under the mutex; counters atomic.
- `wait` participating in execution avoids deadlock when all workers are busy on jobs
  that themselves wait.
- Clean shutdown: set stop flag, notify all, join; queued-but-unstarted jobs after stop
  are not required to run (documented).
- Synchronous (web/0-worker) path: `run` executes inline, counters never go positive
  across a suspend point, `wait`/`parallel_for` just run the bodies.
- Exceptions from a job must not crash a worker (wrap in try/catch, swallow + continue
  — a job throwing shouldn't kill the pool).

## 7. Tests (`tests/test_jobs.cpp`)
parallel_for sums/という fills an array correctly (compare to serial); run+wait single
job; many jobs incrementing an atomic = exact count; nested jobs (a job submits and
waits on children) completes without deadlock; `worker_count()==0` synchronous path
gives identical results; stress (thousands of tiny jobs). Default suite uses the real
pool; a separate TSan build re-runs it for race detection.

## 8. Guidebook (split)
- **41 — Threads, the pool & the queue:** why parallelism, the thread pool, the
  mutex/condition_variable work queue, clean shutdown, the data-race hazard.
- **42 — Counters, wait-and-help & the web fallback:** the counter model, `parallel_for`,
  why `wait` helps run jobs, and the synchronous Emscripten path. C acceptance.

## 9. Risks
- Threading bugs are subtle → TSan gate + a focused cpp/concurrency review before merge.
- Allocators from A are not thread-safe → jobs must use per-thread/local scratch or
  thread-safe std containers; documented (the per-thread-ownership pattern from ch34).
- Keep it opt-in; nothing in the existing engine is forced onto threads.
