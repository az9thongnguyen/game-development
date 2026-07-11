// =============================================================================
//  games/runner/runner.hpp  —  headless scenario execution for the test-runner
// =============================================================================
//  Pure (no SDL, no network): runs a sandbox scene deterministically and reports
//  pass/fail. A "scenario" is a `sandbox1` scene (the slice-1 save format); params
//  are `steps=<N>;expect_alive=<K>`. Pass iff after N fixed ticks the actor count
//  equals K. This is what the --runner worker executes for each claimed job; it
//  links no SDK so it unit-tests headless (tests/test_runner.cpp).
// =============================================================================
#pragma once
#include <string>

namespace runner {

struct RunOutcome {
    std::string status;   // "passed" | "failed" | "error"
    std::string result;   // short human-readable summary
};

// Run `scenario` (a sandbox1 scene) for the params' `steps` fixed ticks and check
// `expect_alive`. Malformed scenario/params → {"error", why}.
RunOutcome run_scenario(const std::string& scenario, const std::string& params);

}  // namespace runner
