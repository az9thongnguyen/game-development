// =============================================================================
//  tests/test_sandbox.cpp  —  sandbox pure-core tests (dependency-free, CTest)
// =============================================================================
//  Covers the declarative sandbox core: spawn wiring, the deterministic tick
//  systems (mover/spinner/bouncer/lifetime/spawner/overlap), and the text
//  serializer used for both save/load and the Play/Stop snapshot. No SDL/window.
// =============================================================================
#include "games/sandbox/world.hpp"
#include "games/sandbox/serialize.hpp"

#include <cmath>
#include <cstdio>

using namespace sandbox;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool approx(double a, double b, double eps = 1e-4) {
    return std::fabs(a - b) <= eps * (1.0 + std::fabs(a) + std::fabs(b));
}

// ---- Task 1: spawn ---------------------------------------------------------
static void test_spawn_attaches() {
    World w;
    Archetype a; a.mover = true; a.vx = 5; a.bouncer = true; a.tag = 3;
    ecs::Entity e = w.spawn(a, 10, 20);
    CHECK(w.alive() == 1);
    CHECK(w.reg.has<Transform2D>(e) && w.reg.has<Body>(e) && w.reg.has<Sprite>(e));
    CHECK(w.reg.has<Mover>(e) && w.reg.has<Bouncer>(e) && w.reg.has<Tag>(e));
    CHECK(!w.reg.has<Spinner>(e) && !w.reg.has<Lifetime>(e));
    CHECK(w.reg.get<Transform2D>(e)->x == 10 && w.reg.get<Mover>(e)->vx == 5);
    CHECK(w.reg.get<Tag>(e)->id == 3);
}

// ---- Task 2: tick systems --------------------------------------------------
static void test_mover_integrates_and_deterministic() {
    World w; Archetype a; a.mover = true; a.vx = 10; a.vy = -4;
    ecs::Entity e = w.spawn(a, 100, 100);
    w.tick(0.5f);
    CHECK(approx(w.reg.get<Transform2D>(e)->x, 105.0));
    CHECK(approx(w.reg.get<Transform2D>(e)->y, 98.0));
    // determinism: two worlds with an IDENTICAL history end bit-identical.
    World d1; ecs::Entity h1 = d1.spawn(a, 100, 100);
    World d2; ecs::Entity h2 = d2.spawn(a, 100, 100);
    for (int i = 0; i < 10; ++i) { d1.tick(0.016f); d2.tick(0.016f); }
    CHECK(d1.reg.get<Transform2D>(h1)->x == d2.reg.get<Transform2D>(h2)->x);
}
static void test_spinner() {
    World w; Archetype a; a.spinner = true; a.omega = 2;
    ecs::Entity e = w.spawn(a, 0, 0); w.tick(0.5f);
    CHECK(approx(w.reg.get<Transform2D>(e)->rot, 1.0));
}
static void test_bouncer_reflects() {
    World w; w.bounds_w = 200; w.bounds_h = 200;
    Archetype a; a.mover = true; a.vx = 100; a.vy = 0; a.bouncer = true; a.w = 20; a.h = 20;
    ecs::Entity e = w.spawn(a, 195, 100);              // 195+10 > 200 -> hits right wall
    w.tick(0.1f);
    CHECK(w.reg.get<Mover>(e)->vx < 0);                // x-vel flipped
    CHECK(w.reg.get<Mover>(e)->vy == 0);              // y untouched
    CHECK(w.reg.get<Transform2D>(e)->x <= 190);       // clamped inside
}
static void test_lifetime_despawns() {
    World w; Archetype a; a.lifetime = true; a.ttl = 0.05f;
    w.spawn(a, 0, 0); CHECK(w.alive() == 1);
    w.tick(0.1f);     CHECK(w.alive() == 0);
}
static void test_spawner() {
    World w; Archetype ball; ball.w = 4; ball.h = 4;
    Archetype emitter; Spawner sp; sp.interval = 1.0f; sp.proto = ball;
    ecs::Entity e = w.spawn(emitter, 50, 60);
    w.reg.add<Spawner>(e, sp);
    for (int i = 0; i < 10; ++i) w.tick(0.34f);        // 3.4s -> 3 spawns
    CHECK(w.alive() == 4);                             // emitter + 3
}
static void test_overlap_trigger() {
    World w;
    Archetype coin; coin.tag = 1; w.spawn(coin, 100, 100);
    Archetype sweeper; OnOverlap o; o.other_tag = 1; o.action = Action::DestroyOther;
    ecs::Entity s = w.spawn(sweeper, 100, 100); w.reg.add<OnOverlap>(s, o);
    w.tick(0.016f);
    CHECK(w.alive() == 1);                             // coin gone, sweeper stays
    CHECK(w.reg.valid(s));
    // no overlap -> nothing dies
    World w2; w2.spawn(coin, 0, 0);
    ecs::Entity s2 = w2.spawn(sweeper, 400, 400); w2.reg.add<OnOverlap>(s2, o);
    w2.tick(0.016f);
    CHECK(w2.alive() == 2);
}

// ---- Task 3: serializer ----------------------------------------------------
static void test_archetype_codec() {
    Archetype a; a.color = gfx::rgb(0x40, 0xcc, 0xff); a.round = true;
    a.mover = true; a.vx = 12; a.vy = -3; a.bouncer = true; a.tag = 2; a.w = 18;
    Archetype b = parse_archetype(archetype_tokens(a));
    CHECK(archetype_tokens(a) == archetype_tokens(b));  // stable round-trip
    CHECK(b.mover && b.vx == 12 && b.bouncer && b.tag == 2 && b.round);
}
static void test_scene_roundtrip() {
    World w; w.bounds_w = 800; w.bounds_h = 600;
    Archetype ball; ball.mover = true; ball.vx = 40; ball.vy = 30; ball.bouncer = true; ball.round = true;
    w.spawn(ball, 100, 120);
    Archetype coin; coin.tag = 1; w.spawn(coin, 300, 200);
    const std::string once = to_scene(w);
    World reparsed = from_scene(once);
    CHECK(to_scene(reparsed) == once);                  // text round-trip
    // trajectory fidelity: two restores tick identically
    World a = from_scene(once), b = from_scene(once);
    for (int i = 0; i < 50; ++i) { a.tick(0.016f); b.tick(0.016f); }
    CHECK(to_scene(a) == to_scene(b));
}
static void test_scene_with_behaviors_roundtrip() {
    World w;
    Archetype emitter; ecs::Entity e = w.spawn(emitter, 60, 60);
    Archetype small; small.w = 6; small.h = 6; small.mover = true; small.vy = 60; small.lifetime = true; small.ttl = 3;
    Spawner sp; sp.interval = 1.5f; sp.proto = small;
    w.reg.add<Spawner>(e, sp);
    Archetype sweeper; ecs::Entity s = w.spawn(sweeper, 100, 100);
    OnOverlap o; o.other_tag = 1; o.action = Action::SpawnProto; o.proto = small;
    w.reg.add<OnOverlap>(s, o);
    const std::string once = to_scene(w);
    CHECK(to_scene(from_scene(once)) == once);          // proto sub-records round-trip
}
// ---- textured sprites: a texture is a name that round-trips through the codec ----
static void test_spawn_copies_texture() {
    World w; Archetype a; a.texture = "studio_03";
    ecs::Entity e = w.spawn(a, 0, 0);
    CHECK(w.reg.get<Sprite>(e)->texture == "studio_03");
    Archetype b; ecs::Entity e2 = w.spawn(b, 0, 0);      // default is untextured
    CHECK(w.reg.get<Sprite>(e2)->texture.empty());
}
static void test_archetype_codec_texture() {
    Archetype a; a.texture = "studio_07"; a.mover = true; a.vx = 5;
    Archetype b = parse_archetype(archetype_tokens(a));
    CHECK(b.texture == "studio_07" && b.mover && b.vx == 5);
}
static void test_scene_roundtrip_texture() {
    World w; Archetype a; a.texture = "studio_02"; w.spawn(a, 10, 20);
    const std::string s = to_scene(w);
    CHECK(s.find("tex=studio_02") != std::string::npos);  // token present
    CHECK(to_scene(from_scene(s)) == s);                   // round-trips
    World u; Archetype b; u.spawn(b, 0, 0);                // untextured emits no token
    CHECK(to_scene(u).find("tex=") == std::string::npos);
}
static void test_snapshot_restore() {
    World w; Archetype ball; ball.mover = true; ball.vx = 40; ball.bouncer = true;
    w.spawn(ball, 50, 50);
    const std::string snap = to_scene(w);
    for (int i = 0; i < 100; ++i) w.tick(0.016f);
    World restored = from_scene(snap);
    CHECK(to_scene(restored) == snap);                  // Stop restores the placed state
}

int main() {
    test_spawn_attaches();
    test_mover_integrates_and_deterministic();
    test_spinner();
    test_bouncer_reflects();
    test_lifetime_despawns();
    test_spawner();
    test_overlap_trigger();
    test_archetype_codec();
    test_scene_roundtrip();
    test_scene_with_behaviors_roundtrip();
    test_spawn_copies_texture();
    test_archetype_codec_texture();
    test_scene_roundtrip_texture();
    test_snapshot_restore();
    if (g_failures == 0) std::printf("sandbox: all tests passed\n");
    else                 std::printf("sandbox: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
