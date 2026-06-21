// =============================================================================
//  engine/jobs/job_system.cpp  —  thread pool implementation
// =============================================================================
#include "engine/jobs/job_system.hpp"

#include <algorithm>
#include <utility>

namespace jobs {

JobSystem::JobSystem(int worker_count) {
#ifdef __EMSCRIPTEN__
    worker_count_ = 0;                                   // web: synchronous, no threads
    (void)worker_count;
#else
    if (worker_count < 0) {
        const unsigned hw = std::thread::hardware_concurrency();
        worker_count_ = (hw > 1) ? static_cast<int>(hw) - 1 : 0;   // leave one for main
    } else {
        worker_count_ = worker_count;
    }
    workers_.reserve(static_cast<std::size_t>(worker_count_));
    for (int i = 0; i < worker_count_; ++i)
        workers_.emplace_back([this] { worker_loop(); });
#endif
}

JobSystem::~JobSystem() {
    wait_idle();                         // finish all queued work while we're fully alive
    {
        std::lock_guard<std::mutex> lk(mutex_);
        stop_.store(true);
    }
    cv_.notify_all();
    for (std::thread& t : workers_)
        if (t.joinable()) t.join();      // queue already drained; workers just exit
}

void JobSystem::run_task(Task& t) {
    try {
        if (t.fn) t.fn();
    } catch (...) {
        // A throwing job must not take down the worker/pool. Swallow and carry on.
    }
    {
        // Decrement counters under the mutex so a waiter's predicate + sleep can't
        // race the notify (no lost wakeups).
        std::lock_guard<std::mutex> lk(mutex_);
        if (t.counter) t.counter->pending.fetch_sub(1);
        total_pending_.fetch_sub(1);
    }
    cv_.notify_all();
}

void JobSystem::worker_loop() {
    for (;;) {
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait(lk, [this] { return stop_.load() || !queue_.empty(); });
        if (stop_.load() && queue_.empty()) return;      // shut down once drained
        Task t = std::move(queue_.front());
        queue_.pop_front();
        lk.unlock();
        run_task(t);
    }
}

void JobSystem::run(Counter& counter, std::function<void()> fn) {
    if (worker_count_ == 0) {                             // synchronous executor
        try { if (fn) fn(); } catch (...) {}
        return;
    }
    {
        std::lock_guard<std::mutex> lk(mutex_);
        counter.pending.fetch_add(1);
        total_pending_.fetch_add(1);
        queue_.push_back(Task{std::move(fn), &counter});
    }
    cv_.notify_one();
}

void JobSystem::run(std::function<void()> fn) { run(internal_, std::move(fn)); }

void JobSystem::wait_on(std::atomic<int>& counter) {
    if (worker_count_ == 0) return;                      // synchronous: nothing pending
    for (;;) {
        if (counter.load() == 0) return;
        std::unique_lock<std::mutex> lk(mutex_);
        if (!queue_.empty()) {
            Task t = std::move(queue_.front());          // help: run a queued job
            queue_.pop_front();
            lk.unlock();
            run_task(t);
        } else {
            // No work to help with: sleep until work arrives or the counter is done.
            cv_.wait(lk, [this, &counter] { return !queue_.empty() || counter.load() == 0; });
        }
    }
}

void JobSystem::wait(Counter& counter) { wait_on(counter.pending); }
void JobSystem::wait_idle()            { wait_on(total_pending_); }

void JobSystem::parallel_for(std::size_t n, const std::function<void(std::size_t)>& body,
                             std::size_t grain) {
    if (n == 0) return;
    if (grain == 0) grain = 1;

    if (worker_count_ == 0) {                            // synchronous
        for (std::size_t i = 0; i < n; ++i) body(i);
        return;
    }

    Counter c;
    for (std::size_t start = 0; start < n; start += grain) {
        const std::size_t end = std::min(start + grain, n);
        run(c, [&body, start, end] {                     // body outlives: we wait() below
            for (std::size_t i = start; i < end; ++i) body(i);
        });
    }
    wait(c);
}

} // namespace jobs
