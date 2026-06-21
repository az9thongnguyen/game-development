# Chapter 41 — Threads, the Pool & the Queue

> **What this is.** Subsystem **C**, part one: how to run work on multiple CPU cores
> safely. We build a **thread pool** — a fixed set of worker threads pulling tasks off
> a shared, mutex-protected queue — and learn the concurrency primitives
> (`std::thread`, `std::mutex`, `std::condition_variable`, `std::atomic`) and the
> data-race hazard they exist to prevent. The counter/`parallel_for` API is chapter
> 42. Code: `src/engine/jobs/job_system.{hpp,cpp}`.

---

## 1. Why a job system (and the web tension)

A modern CPU has many cores; a single-threaded game uses one. A **job system** spreads
independent work — animating 10k characters, integrating 5k rigid bodies, culling a
scene — across cores for a near-linear speedup.

But the engine has a standing rule: **single-threaded, web-first** (Emscripten threads
need special headers/flags). We reconcile with **one API, two executors**:

- **Desktop** → a real thread pool.
- **Web (or 0 workers)** → **synchronous**: a "job" just runs immediately on the
  caller. No threads, so the WASM build stays a drop-in.

So parallelism is *opt-in* and degrades to plain sequential code on the web. The loop
above the platform never changes.

## 2. The data-race hazard (why we need locks)

If two threads touch the same memory and at least one writes, with no synchronization,
that's a **data race** — undefined behavior (torn values, lost updates, crashes). The
classic example:

```
two threads run:  count = count + 1;    // read, add, write — NOT atomic
result: sometimes count goes up by 1 instead of 2 (one update is lost)
```

Two tools fix it:

- **`std::atomic<T>`** — operations like `fetch_add` are indivisible; perfect for a
  counter many threads bump.
- **`std::mutex`** — only one thread holds it at a time; wrap any *multi-step* shared
  update (like pushing onto a queue) in a lock so others can't see a half-done state.

We verify the whole system is race-free with **ThreadSanitizer** (a separate build —
TSan can't combine with AddressSanitizer).

## 3. The thread pool

A pool creates its threads **once** and reuses them — spawning a thread per task would
cost more than the task. Workers spin in a loop: wait for work, take a task, run it,
repeat.

```cpp
JobSystem::JobSystem(int worker_count) {
#ifdef __EMSCRIPTEN__
    worker_count_ = 0;                      // web: synchronous, spawn nothing
#else
    worker_count_ = (worker_count < 0)
        ? std::max(0, (int)std::thread::hardware_concurrency() - 1)  // leave one for main
        : worker_count;
    for (int i = 0; i < worker_count_; ++i)
        workers_.emplace_back([this] { worker_loop(); });
#endif
}
```

We use `hardware_concurrency() - 1` so the main thread still gets a core.

## 4. The work queue (mutex + condition_variable)

Tasks live in a `std::deque<Task>` guarded by a mutex. Workers must **sleep** when
there's no work (busy-waiting would peg every core at 100%). That's what a
**condition variable** is for: a worker waits on it; submitting work notifies it.

```cpp
void JobSystem::worker_loop() {
    for (;;) {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return stop_.load() || !queue_.empty(); });  // sleep until work/stop
        if (stop_.load() && queue_.empty()) return;     // shut down once drained
        Task t = std::move(queue_.front());
        queue_.pop_front();
        lk.unlock();                                    // run OUTSIDE the lock (parallelism!)
        run_task(t);
    }
}
```

Two essentials:

- **`cv_.wait(lk, predicate)`** atomically releases the lock and sleeps, and on wake
  re-checks the predicate under the lock — this is the guard against **spurious
  wakeups** and **lost wakeups**.
- **Run the task with the lock released.** Holding the mutex while running a job would
  serialize everything — the opposite of what we want. Only the queue *access* is
  locked.

Submitting mirrors it — modify shared state under the lock, then notify:

```cpp
{ std::lock_guard<std::mutex> lk(mutex_); /* counter++, total++ */ queue_.push_back(...); }
cv_.notify_one();
```

## 5. Avoiding lost wakeups

A *lost wakeup* is the nastiest pool bug: a waiter checks "no work yet", and just
before it sleeps, a producer adds work and notifies — into the void — so the waiter
sleeps forever. The fix is the rule: **every change to the state a waiter's predicate
reads must happen under the same mutex the waiter holds in `cv_.wait`.** We push tasks
under the lock, and (chapter 42) we decrement job counters under the lock too. The
`cv_.wait(lk, pred)` form then guarantees no notification is missed.

## 6. Clean shutdown

The destructor must stop the workers without leaking threads or dropping work
mid-flight:

```cpp
JobSystem::~JobSystem() {
    wait_idle();                                  // finish queued work while alive
    { std::lock_guard<std::mutex> lk(mutex_); stop_.store(true); }
    cv_.notify_all();                             // wake every sleeping worker
    for (std::thread& t : workers_) if (t.joinable()) t.join();   // wait for them to exit
}
```

`wait_idle()` first (chapter 42) drains outstanding jobs deterministically while the
object is fully alive; then `stop_` + `notify_all` + `join` ends the threads. A worker
exits its loop only when `stop_` is set **and** the queue is empty, so nothing queued
is silently dropped. (Joining a thread = "block until it finishes"; never `detach` a
worker — that risks it touching freed state after the pool dies.)

## 7. Pitfalls

- **Busy-waiting.** Polling a flag in a loop burns a core. Sleep on a condition
  variable instead.
- **Running tasks under the lock.** Serializes the pool. Lock only the queue access.
- **Forgetting the predicate in `cv_.wait`.** The lambda form handles spurious *and*
  lost wakeups; the no-predicate overload doesn't.
- **`detach`ing workers.** Use `join` in the destructor — detached threads outlive the
  object and cause use-after-free.
- **Jobs that throw.** A thrown exception escaping a worker would terminate the
  program; `run_task` wraps each job in try/catch (jobs shouldn't throw — chapter 42).

## 8. Glossary

- **Thread pool** — a reusable set of worker threads consuming tasks.
- **Data race** — unsynchronized concurrent access (≥1 write) = UB.
- **Mutex / lock** — mutual exclusion for multi-step shared updates.
- **Condition variable** — sleep until notified; the right way to wait for work.
- **Lost / spurious wakeup** — a missed notify / a wake with no cause; the predicate
  form of `cv_.wait` defends against both.
- **join** — block until a thread finishes (clean shutdown).

## 9. Exercises

1. **Count the cores.** Print `hardware_concurrency()` and the chosen worker count.
2. **Feel the race.** Have 8 raw threads do `g += 1` a million times each *without* a
   lock/atomic; observe the total is wrong. Then fix it with `std::atomic`.
3. **Busy vs sleep.** Replace the condition variable with a spin loop and watch CPU
   usage; then revert. Why is the cv version better?
4. **Shutdown order.** Swap the destructor to `notify_all` *before* setting `stop_`;
   explain why that can hang.

*(Next: chapter 42 — counters, wait-and-help, parallel_for, and the web fallback.)*
