// =============================================================================
//  engine/jobs/job_system.hpp  —  a thread-pool job system (web = synchronous)
// =============================================================================
//  Run work across CPU cores: submit tasks, parallel_for a range, wait on a counter.
//  ONE API, two executors:
//    • desktop  → a real thread pool (hardware_concurrency()-1 workers)
//    • web/0    → synchronous: run() executes inline, wait() is a no-op (no pthreads,
//                 so the Emscripten build stays a drop-in — the engine's web-first rule)
//
//  Model (counters + "wait-and-help"): run(counter, fn) bumps the counter and queues
//  the job; wait(counter) blocks until it hits zero, but WHILE waiting it pops and runs
//  queued jobs itself — so no core idles and a job that waits on child jobs can't
//  deadlock the pool. All shared-state changes happen under one mutex (no lost wakeups).
// =============================================================================
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace jobs {

// An atomic count of outstanding jobs. wait()/parallel_for block until it reaches 0.
struct Counter {
    std::atomic<int> pending{0};
};

// Contract / limitations (single-binary, opt-in parallelism):
//   • Jobs SHOULD NOT throw — exceptions are swallowed so one bad job can't kill a
//     worker. If a job can fail, capture a result/error flag in its closure.
//   • wait()/parallel_for "help" by running queued jobs on the calling thread, so a
//     job that waits on children nests on the C++ stack. Normal graphs (parallel_for,
//     a couple of levels) are fine; pathologically deep RECURSIVE job graphs can
//     exhaust the stack — those need a fiber scheduler (an exercise), not this.
//   • The destructor drains all queued work (wait_idle) before joining. Do NOT call
//     into a JobSystem from another thread while it is being destroyed.
class JobSystem {
public:
    // worker_count: -1 => hardware_concurrency()-1 (at least 0); 0 => synchronous.
    // Under Emscripten it is forced to 0 (synchronous) regardless.
    explicit JobSystem(int worker_count = -1);
    ~JobSystem();

    JobSystem(const JobSystem&)            = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void run(Counter& counter, std::function<void()> fn);   // counted task
    void run(std::function<void()> fn);                     // fire-and-forget
    void wait(Counter& counter);                            // block + help until 0
    void wait_idle();                                       // drain ALL queued work

    // Run body(i) for i in [0,n), split into chunks of `grain`, then wait.
    void parallel_for(std::size_t n, const std::function<void(std::size_t)>& body,
                      std::size_t grain = 1);

    int worker_count() const { return worker_count_; }

private:
    struct Task {
        std::function<void()> fn;
        Counter*              counter = nullptr;   // counted task's counter, or nullptr
    };

    void worker_loop();
    void run_task(Task& t);            // execute fn, then decrement counters + notify
    void wait_on(std::atomic<int>& counter);

    std::vector<std::thread>  workers_;
    std::deque<Task>          queue_;
    std::mutex                mutex_;
    std::condition_variable   cv_;
    std::atomic<bool>         stop_{false};
    std::atomic<int>          total_pending_{0};  // every queued job (for wait_idle)
    Counter                   internal_;          // fire-and-forget jobs
    int                       worker_count_ = 0;
};

} // namespace jobs
