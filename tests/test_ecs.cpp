// =============================================================================
//  tests/test_ecs.cpp  —  engine-core ECS (entities, components, views; no SDL)
// =============================================================================
#include "engine/ecs/registry.hpp"

#include <cstdio>

using ecs::Entity;
using ecs::Registry;

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

struct Position { float x = 0, y = 0; };
struct Velocity { float x = 0, y = 0; };
struct Tag      { int v = 0; };

static void test_entities() {
    Registry r;
    CHECK(r.alive() == 0 && !r.valid(ecs::null_entity));

    Entity a = r.create();
    Entity b = r.create();
    CHECK(r.valid(a) && r.valid(b) && a != b && r.alive() == 2);

    const std::uint32_t a_index = a.index;
    r.destroy(a);
    CHECK(!r.valid(a) && r.alive() == 1);          // stale handle now invalid

    Entity c = r.create();                          // should recycle a's slot
    CHECK(c.index == a_index);                      // index reused
    CHECK(c.generation != a.generation);            // but a different generation
    CHECK(r.valid(c) && !r.valid(a));               // old handle still invalid
}

static void test_components() {
    Registry r;
    Entity e = r.create();
    r.add<Position>(e, {1, 2});
    CHECK(r.has<Position>(e) && !r.has<Velocity>(e));
    CHECK(r.get<Position>(e)->x == 1.0f);

    r.add<Position>(e, {9, 9});                      // overwrite
    CHECK(r.get<Position>(e)->x == 9.0f);

    r.remove<Position>(e);
    CHECK(!r.has<Position>(e) && r.get<Position>(e) == nullptr);

    // swap-and-pop survivors stay intact.
    Entity e0 = r.create(), e1 = r.create(), e2 = r.create();
    r.add<Tag>(e0, {10});
    r.add<Tag>(e1, {11});
    r.add<Tag>(e2, {12});
    r.remove<Tag>(e1);
    CHECK(r.get<Tag>(e0)->v == 10 && r.get<Tag>(e2)->v == 12 && !r.has<Tag>(e1));
}

static void test_destroy_clears_components() {
    Registry r;
    Entity e = r.create();
    r.add<Position>(e, {1, 1});
    r.add<Velocity>(e, {2, 2});
    r.destroy(e);
    CHECK(!r.has<Position>(e) && !r.has<Velocity>(e));
    // The recycled slot starts clean.
    Entity e2 = r.create();
    CHECK(e2.index == e.index && !r.has<Position>(e2));
}

static void test_view() {
    Registry r;
    Entity m1 = r.create(), m2 = r.create(), only_p = r.create();
    r.add<Position>(m1, {0, 0}); r.add<Velocity>(m1, {1, 1});
    r.add<Position>(m2, {0, 0}); r.add<Velocity>(m2, {2, 2});
    r.add<Position>(only_p, {5, 5});                 // no Velocity → excluded

    int matched = 0;
    r.view<Position, Velocity>([&](Entity, Position& p, Velocity& v) {
        p.x += v.x; p.y += v.y;
        ++matched;
    });
    CHECK(matched == 2);                             // only_p excluded
    CHECK(r.get<Position>(m1)->x == 1.0f && r.get<Position>(m2)->x == 2.0f);
    CHECK(r.get<Position>(only_p)->x == 5.0f);       // untouched

    // Single-component view.
    int count_p = 0;
    r.view<Position>([&](Entity, Position&) { ++count_p; });
    CHECK(count_p == 3);

    // View over a component nobody has → zero iterations, no crash.
    int none = 0;
    r.view<Tag>([&](Entity, Tag&) { ++none; });
    CHECK(none == 0);
}

static void test_stale_safety() {
    Registry r;
    Entity e = r.create();
    r.add<Position>(e, {1, 1});
    r.destroy(e);
    // All component ops on a stale/invalid handle are safe no-ops.
    CHECK(r.get<Position>(e) == nullptr);
    CHECK(!r.has<Position>(e));
    r.remove<Position>(e);            // no-op, must not crash
    r.destroy(e);                     // double destroy is a safe no-op
    CHECK(r.alive() == 0);
}

int main() {
    test_entities();
    test_components();
    test_destroy_clears_components();
    test_view();
    test_stale_safety();
    if (g_failures == 0) std::printf("ecs: all tests passed\n");
    else                 std::printf("ecs: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
