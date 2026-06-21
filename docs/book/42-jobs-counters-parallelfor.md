# Chapter 42 — Counters, Wait-and-Help & the Web Fallback

> **What this is.** Subsystem **C**, part two: how you actually *use* the pool. The
> **counter** model tracks outstanding work; **`parallel_for`** is the everyday
> data-parallel tool; **wait-and-help** keeps cores busy and prevents deadlock; and
> the **synchronous web path** makes it all degrade cleanly to single-threaded.
> Code: `src/engine/jobs/job_system.{hpp,cpp}`. Read chapter 41 first.

---

## 1. Counters: knowing when work is done

How do you know a batch of jobs has finished? Track a count. A `Counter` is just an
atomic integer of jobs still pending:

```cpp
struct Counter { std::atomic<int> pending{0}; };
```

`run(counter, fn)` bumps it before queuing; the worker drops it when the job ends:

```cpp
void JobSystem::run(Counter& c, std::function<void()> fn) {
    if (worker_count_ == 0) { fn(); return; }       // synchronous: just run it
    { std::lock_guard<std::mutex> lk(mutex_);
      c.pending.fetch_add(1); total_pending_.fetch_add(1);
      queue_.push_back({std::move(fn), &c}); }
    cv_.notify_one();
}
```

When a job finishes (`run_task`), the counter is decremented **under the mutex** (so a
waiter can't miss the wakeup — chapter 41 §5) and everyone is notified:

```cpp
{ std::lock_guard<std::mutex> lk(mutex_);
  if (t.counter) t.counter->pending.fetch_sub(1);
  total_pending_.fetch_sub(1); }
cv_.notify_all();
```

`total_pending_` counts *all* jobs, so `wait_idle()` can drain everything; a per-batch
`Counter` lets you wait on just your batch.

## 2. Wait-and-help (the clever bit)

A naive `wait` would sleep until the counter hits zero. But what if you're a job
*running on a worker*, waiting on child jobs, and every other worker is also busy
waiting? Nobody's left to run the children → **deadlock**.

The fix: while waiting, **help** — pop and run queued jobs yourself:

```cpp
void JobSystem::wait_on(std::atomic<int>& counter) {
    if (worker_count_ == 0) return;                  // synchronous: nothing pending
    for (;;) {
        if (counter.load() == 0) return;             // done
        std::unique_lock<std::mutex> lk(mutex_);
        if (!queue_.empty()) {
            Task t = std::move(queue_.front());       // HELP: run a queued job myself
            queue_.pop_front(); lk.unlock();
            run_task(t);
        } else {
            cv_.wait(lk, [&] { return !queue_.empty() || counter.load() == 0; });
        }
    }
}
```

Now a waiting thread is never idle while work exists, and a job can safely wait on its
own children even on a one-worker pool — the waiter runs them. (Proven by the
`worker_count = 1` nested test.)

> **Limit:** because helping nests on the C++ stack, a *pathologically deep* recursive
> job graph could exhaust the stack. Normal graphs (a `parallel_for`, a couple of
> nesting levels) are fine; true unbounded recursion needs a *fiber* scheduler — a
> different architecture, left as an exercise.

## 3. parallel_for: the everyday tool

99% of the time you don't hand-submit jobs — you parallelize a loop. `parallel_for`
splits `[0, n)` into chunks of `grain`, runs each chunk as a job, and waits:

```cpp
void JobSystem::parallel_for(std::size_t n,
                             const std::function<void(std::size_t)>& body,
                             std::size_t grain) {
    if (n == 0) return;
    if (worker_count_ == 0) { for (std::size_t i=0;i<n;++i) body(i); return; }  // sync
    Counter c;
    for (std::size_t s = 0; s < n; s += grain) {
        const std::size_t e = std::min(s + grain, n);
        run(c, [&body, s, e] { for (std::size_t i=s;i<e;++i) body(i); });
    }
    wait(c);                                          // block (and help) until all chunks done
}
```

Usage — each index writes its own slot, so there's no sharing and no lock needed:

```cpp
js.parallel_for(positions.size(), [&](std::size_t i) {
    positions[i] += velocities[i] * dt;              // embarrassingly parallel
}, /*grain=*/256);
```

**Grain size** trades scheduling overhead against load balance: too small = lots of
tiny jobs (overhead); too big = stragglers (poor balance). A few hundred elements per
chunk is a sane default.

> **No accidental sharing.** `parallel_for` only parallelizes correctly when chunks
> are *independent*. Writing the same memory from two chunks is a data race — use an
> atomic, a reduction, or partition the data.

## 4. The web (synchronous) fallback

Every public method has a `worker_count_ == 0` branch that runs work inline:
`run` calls `fn()` immediately, `wait`/`wait_idle` are no-ops, `parallel_for` is a plain
loop. Under Emscripten the constructor forces `worker_count_ = 0`, so the WASM build
gets identical *results* with zero threads — no pthreads, no cross-origin-isolation
headers, no engine changes. The same game code that uses `parallel_for` on desktop
runs single-threaded in the browser. That is the web-first invariant being honored by
the executor abstraction, not by the call sites.

## 5. Using it from systems

A "system" stays a plain function; it just parallelizes its hot loop:

```cpp
void integrate(jobs::JobSystem& js, std::vector<Body>& bodies, float dt) {
    js.parallel_for(bodies.size(), [&](std::size_t i) {
        bodies[i].pos += bodies[i].vel * dt;
    }, 256);
}
```

On desktop this fans out across cores; on web it's a sequential loop — same answer.

## 6. C acceptance

- [x] **Thread pool** with `hardware_concurrency()-1` workers; clean join on shutdown.
- [x] **Counters + run/wait/wait_idle**; counts exact under heavy load.
- [x] **parallel_for** matches the serial result (tested over 10k elements).
- [x] **Wait-and-help**: nested jobs complete with no deadlock, even on a 1-worker pool.
- [x] **Synchronous web path**: `worker_count==0` yields identical results inline.
- [x] **Race-free**: ThreadSanitizer clean; warning-clean; no SDL.

Verified by `tests/test_jobs.cpp` (parallel_for correctness, run/wait counts, wait_idle,
nested, single-worker nested, synchronous) plus a dedicated TSan build.

## 7. Glossary

- **Counter** — atomic count of outstanding jobs; the completion signal.
- **wait-and-help** — a waiter runs queued jobs instead of idling (prevents deadlock).
- **parallel_for / grain** — split a range into independent chunks of `grain` and run
  them in parallel.
- **Synchronous executor** — the 0-worker / web path that runs jobs inline.

## 8. Exercises

1. **Parallel reduce.** Sum a big array with `parallel_for` into per-chunk partials,
   then combine — avoiding a shared accumulator. Compare timing to the serial sum.
2. **Tune the grain.** Time `parallel_for` over 1M trivial items at grain 1, 64, 4096.
   Plot the sweet spot.
3. **Adopt it.** Parallelize the M2 raycaster's per-column loop with `parallel_for`;
   confirm identical output and a speedup, and that the web build still runs.
4. **Future/await.** Add a `submit<T>(fn) -> std::future<T>` returning a result, and
   compare with the counter model.

*(Subsystem C complete. Next: D — the asset pipeline + hot reload.)*
