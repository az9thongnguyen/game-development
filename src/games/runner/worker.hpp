// =============================================================================
//  games/runner/worker.hpp  —  the test-run worker (claims + executes one job)
// =============================================================================
//  The worker is where the two halves meet: it links the SDK (to talk to the
//  BaaS coordinator) AND the engine (to actually run a scenario) — which the BaaS
//  itself may never do. process_one() does one unit of work; `demo --runner`
//  loops it. Kept small + synchronous (pumps the client internally) so it is
//  drive-able from a test against a live server.
// =============================================================================
#pragma once

namespace gbaas { class Client; }

namespace runner {

// Claim and execute one pending run against the coordinator `c`, posting its
// result. Returns true if it did work, false if nothing was pending (or a claim
// was lost to another worker). Blocks (pumps the client) until each step lands.
bool process_one(gbaas::Client& c);

}  // namespace runner
