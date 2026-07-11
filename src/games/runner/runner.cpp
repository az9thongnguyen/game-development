// =============================================================================
//  games/runner/runner.cpp  —  see runner.hpp
// =============================================================================
#include "games/runner/runner.hpp"

#include <optional>

#include "games/sandbox/serialize.hpp"
#include "games/sandbox/world.hpp"

namespace runner {

namespace {
constexpr int kMaxSteps = 100000;   // ponytail: bound worker time; a scenario can't run forever

// Read `key=<int>` from a `k=v;k=v` params string. nullopt if the key is absent
// or its value isn't an integer.
std::optional<long> param_int(const std::string& params, const std::string& key) {
    const std::string needle = key + "=";
    std::size_t pos = 0;
    while ((pos = params.find(needle, pos)) != std::string::npos) {
        // must be at start or right after a ';' (so "expect_alive" != "alive")
        if (pos == 0 || params[pos - 1] == ';') break;
        pos += needle.size();
    }
    if (pos == std::string::npos) return std::nullopt;
    pos += needle.size();
    std::size_t end = params.find(';', pos);
    const std::string val = params.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    if (val.empty()) return std::nullopt;
    try { return std::stol(val); } catch (...) { return std::nullopt; }
}
}  // namespace

RunOutcome run_scenario(const std::string& scenario, const std::string& params) {
    if (scenario.rfind("sandbox1", 0) != 0)
        return {"error", "scenario is not a sandbox1 scene"};

    const auto expect = param_int(params, "expect_alive");
    if (!expect)
        return {"error", "params missing expect_alive"};
    long steps = param_int(params, "steps").value_or(60);
    if (steps < 0) steps = 0;
    if (steps > kMaxSteps) steps = kMaxSteps;

    try {
        sandbox::World w = sandbox::from_scene(scenario);
        for (long i = 0; i < steps; ++i) w.tick(1.0f / 60.0f);
        const long alive = static_cast<long>(w.alive());
        const std::string summary =
            "alive=" + std::to_string(alive) + " expected=" + std::to_string(*expect) +
            " after " + std::to_string(steps) + " steps";
        return {alive == *expect ? "passed" : "failed", summary};
    } catch (...) {
        return {"error", "scenario execution threw"};
    }
}

}  // namespace runner
