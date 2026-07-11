// =============================================================================
//  tests/test_runner.cpp  —  headless scenario execution (dependency-free, CTest)
// =============================================================================
//  run_scenario runs a sandbox1 scene for N deterministic ticks and checks the
//  actor count against expect_alive. Covers pass, fail, a lifetime-despawn
//  scenario, and malformed input → error. No SDL / network.
// =============================================================================
#include "games/runner/runner.hpp"

#include <cstdio>

#include "games/sandbox/serialize.hpp"
#include "games/sandbox/world.hpp"

static int g_failures = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_failures; } \
    } while (0)

int main() {
    // A scene with one static actor (no mover/lifetime): it never dies.
    sandbox::World stat;
    sandbox::Archetype a;
    stat.spawn(a, 100, 100);
    const std::string scene_static = sandbox::to_scene(stat);

    // passed: after 10 ticks the one actor is still alive
    {
        runner::RunOutcome o = runner::run_scenario(scene_static, "steps=10;expect_alive=1");
        CHECK(o.status == "passed");
        CHECK(o.result.find("alive=1") != std::string::npos);
    }
    // failed: same scene, wrong expectation
    {
        runner::RunOutcome o = runner::run_scenario(scene_static, "steps=10;expect_alive=0");
        CHECK(o.status == "failed");
    }
    // passed via lifetime despawn: a short-ttl actor is gone after enough steps
    {
        sandbox::World life;
        sandbox::Archetype la; la.lifetime = true; la.ttl = 0.05f;
        life.spawn(la, 50, 50);
        const std::string scene_life = sandbox::to_scene(life);
        // 20 ticks = 0.33s > 0.05s ttl → despawns → alive 0
        runner::RunOutcome o = runner::run_scenario(scene_life, "steps=20;expect_alive=0");
        CHECK(o.status == "passed");
    }
    // error: not a sandbox1 scene
    {
        runner::RunOutcome o = runner::run_scenario("garbage", "steps=5;expect_alive=0");
        CHECK(o.status == "error");
    }
    // error: params missing expect_alive
    {
        runner::RunOutcome o = runner::run_scenario(scene_static, "steps=5");
        CHECK(o.status == "error");
    }

    if (g_failures == 0) std::printf("runner: all tests passed\n");
    else                 std::printf("runner: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
