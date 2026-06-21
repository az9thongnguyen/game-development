// =============================================================================
//  tests/test_colony.cpp  —  the colony Sim: ECS + jobs integration (no SDL)
// =============================================================================
#include "games/colony/colony.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool approx(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(a) + std::fabs(b));
}

static void test_spawn_components() {
    colony::Sim s(16, 16, 0);
    const ecs::Entity a = s.spawn_agent(1, 1, 0xFF00FF00);
    s.spawn_prop(5, 5, 0xFFFFFFFF);
    CHECK(s.agent_count() == 1);
    ecs::Registry& r = s.registry();
    CHECK(r.has<colony::GridPos>(a) && r.has<colony::Visual>(a) && r.has<colony::Agent>(a));
    CHECK(r.get<colony::GridPos>(a)->x == 1.0f);
    CHECK(r.get<colony::Visual>(a)->is_agent);
}

static void test_goal_and_movement() {
    colony::Sim s(16, 16, 0);                       // synchronous
    const ecs::Entity a = s.spawn_agent(0, 0, 0xFF00FF00);
    s.set_goal(10, 0);
    CHECK(!s.registry().get<colony::Agent>(a)->path.empty());   // got an A* path
    for (int i = 0; i < 1200; ++i) s.update(1.0 / 60.0);
    const colony::GridPos* p = s.registry().get<colony::GridPos>(a);
    CHECK(approx(p->x, 10.0f, 0.05f) && approx(p->y, 0.0f, 0.05f));   // arrived
}

static void test_serial_vs_parallel() {
    // Same inputs through the synchronous and the threaded executor must match.
    colony::Sim serial(20, 20, 0);
    colony::Sim parallel(20, 20, -1);
    std::vector<ecs::Entity> es, ep;
    for (int i = 0; i < 64; ++i) {
        es.push_back(serial.spawn_agent(i % 20, i / 20, 0xFFFFFFFF));
        ep.push_back(parallel.spawn_agent(i % 20, i / 20, 0xFFFFFFFF));
    }
    serial.set_goal(19, 19);
    parallel.set_goal(19, 19);
    for (int i = 0; i < 200; ++i) { serial.update(1.0 / 60.0); parallel.update(1.0 / 60.0); }
    for (std::size_t i = 0; i < es.size(); ++i) {
        const auto* a = serial.registry().get<colony::GridPos>(es[i]);
        const auto* b = parallel.registry().get<colony::GridPos>(ep[i]);
        CHECK(a && b && approx(a->x, b->x) && approx(a->y, b->y));
    }
}

static void test_reset() {
    colony::Sim s(20, 20, 0);
    s.spawn_agent(0, 0, 0xFFFFFFFF);
    s.reset_default();
    CHECK(s.agent_count() == 6);                    // default scene's agents
    CHECK(!s.walkable(s.width() - 2, s.height() - 2));   // pond tile is water
}

int main() {
    test_spawn_components();
    test_goal_and_movement();
    test_serial_vs_parallel();
    test_reset();
    if (g_failures == 0) std::printf("colony: all tests passed\n");
    else                 std::printf("colony: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
