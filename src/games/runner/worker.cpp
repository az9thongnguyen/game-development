// =============================================================================
//  games/runner/worker.cpp  —  see worker.hpp
// =============================================================================
#include "games/runner/worker.hpp"

#include <chrono>
#include <thread>
#include <vector>

#include "gbaas/client.h"
#include "games/runner/runner.hpp"

namespace runner {

namespace {
// Pump the client until `done` flips or we time out (the SDK is non-blocking; a
// worker step must wait for its callback). Mirrors the SDK test harness.
bool pump(gbaas::Client& c, const bool& done) {
    for (int i = 0; i < 400 && !done; ++i) {
        c.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done;
}
}  // namespace

bool process_one(gbaas::Client& c) {
    // 1. find a pending run
    std::vector<gbaas::TestRun> runs;
    bool                        got = false, done = false;
    c.testruns().list([&](gbaas::Result<std::vector<gbaas::TestRun>> r) {
        if (r) { runs = *r; got = true; }
        done = true;
    });
    if (!pump(c, done) || !got) return false;

    long long id = -1;
    for (const auto& t : runs)
        if (t.status == "pending") { id = t.id; break; }
    if (id < 0) return false;

    // 2. claim it (may be lost to another worker -> just retry next tick)
    gbaas::TestRun claimed;
    bool           claimed_ok = false;
    done = false;
    c.testruns().claim(id, [&](gbaas::Result<gbaas::TestRun> r) {
        if (r) { claimed = *r; claimed_ok = true; }
        done = true;
    });
    if (!pump(c, done) || !claimed_ok) return false;

    // 3. execute the scenario headlessly (engine side)
    const RunOutcome outcome = run_scenario(claimed.scenario, claimed.params);

    // 4. post the result back to the coordinator
    done = false;
    c.testruns().complete(id, outcome.status, outcome.result,
                          [&](gbaas::Result<bool>) { done = true; });
    pump(c, done);
    return true;
}

}  // namespace runner
