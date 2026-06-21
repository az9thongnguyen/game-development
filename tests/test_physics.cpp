// =============================================================================
//  tests/test_physics.cpp  —  2D physics: collision, integration, resolution
// =============================================================================
#include "engine/physics/collision.hpp"
#include "engine/physics/world.hpp"

#include <cmath>
#include <cstdio>

using math::vec2;

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

static void test_detect() {
    // circle-circle: overlap & miss.
    phys::Body a = phys::make_body({0, 0}, phys::circle(1.0f));
    phys::Body b = phys::make_body({1.5f, 0}, phys::circle(1.0f));
    phys::Manifold m = phys::detect(a, b);
    CHECK(m.hit && approx(m.penetration, 0.5f) && approx(m.normal.x, 1.0f) && approx(m.normal.y, 0.0f));
    b.pos = {3.0f, 0};
    CHECK(!phys::detect(a, b).hit);

    // box-box: least-penetration axis.
    phys::Body bx1 = phys::make_body({0, 0}, phys::box({1, 1}));
    phys::Body bx2 = phys::make_body({1.5f, 0}, phys::box({1, 1}));
    m = phys::detect(bx1, bx2);
    CHECK(m.hit && approx(m.penetration, 0.5f) && approx(m.normal.x, 1.0f));

    // circle-box: circle just left of a box, overlapping its face.
    phys::Body c  = phys::make_body({-1.4f, 0}, phys::circle(0.5f));
    phys::Body bx = phys::make_body({0, 0}, phys::box({1, 1}));
    m = phys::detect(c, bx);
    CHECK(m.hit && approx(m.normal.x, 1.0f));      // normal c → box points +x
    CHECK(m.penetration > 0.0f);
    // box vs circle flips the normal.
    phys::Manifold m2 = phys::detect(bx, c);
    CHECK(m2.hit && approx(m2.normal.x, -1.0f));
}

static void test_integration() {
    phys::World w;
    w.set_gravity({0, 10.0f});
    const int id = w.add(phys::make_body({0, 0}, phys::circle(0.5f)));
    for (int i = 0; i < 60; ++i) w.step(1.0 / 60.0);   // ~1 second of fall
    CHECK(w.body(id).pos.y > 0.0f && w.body(id).vel.y > 0.0f);   // fell, gaining speed
    CHECK(approx(w.body(id).vel.y, 10.0f, 0.05f));              // v ≈ g·t
}

static void test_static_immovable() {
    phys::World w;
    w.set_gravity({0, 10.0f});
    const int floor = w.add(phys::static_body({0, 5}, phys::box({10, 0.5f})));
    const vec2 before = w.body(floor).pos;
    for (int i = 0; i < 120; ++i) w.step(1.0 / 60.0);
    CHECK(approx(w.body(floor).pos.x, before.x) && approx(w.body(floor).pos.y, before.y));
}

static void test_elastic_exchange() {
    phys::World w;
    w.set_gravity({0, 0});                              // isolate the collision
    const int a = w.add(phys::make_body({0, 0},    phys::circle(1.0f), 1.0f, 1.0f));   // e=1
    const int b = w.add(phys::make_body({1.8f, 0}, phys::circle(1.0f), 1.0f, 1.0f));
    w.body(a).vel = {1.0f, 0};
    w.body(b).vel = {-1.0f, 0};
    w.step(1.0 / 600.0);                               // tiny dt: positions barely move
    // Equal masses, head-on, perfectly elastic ⇒ velocities exchange.
    CHECK(w.body(a).vel.x < -0.5f && w.body(b).vel.x > 0.5f);
}

static void test_inelastic() {
    phys::World w;
    w.set_gravity({0, 0});
    const int a = w.add(phys::make_body({0, 0},    phys::circle(1.0f), 1.0f, 0.0f));   // e=0
    const int b = w.add(phys::make_body({1.8f, 0}, phys::circle(1.0f), 1.0f, 0.0f));
    w.body(a).vel = {1.0f, 0};
    w.body(b).vel = {-1.0f, 0};
    w.step(1.0 / 600.0);
    // Inelastic ⇒ approach velocity removed (no longer closing).
    const float closing = w.body(b).vel.x - w.body(a).vel.x;   // <0 means still approaching
    CHECK(closing >= -1e-2f);
}

static void test_ball_settles_on_floor() {
    phys::World w;
    w.set_gravity({0, 20.0f});
    const int floor = w.add(phys::static_body({0, 10}, phys::box({50, 0.5f}), 0.0f));
    const int ball  = w.add(phys::make_body({0, 0}, phys::circle(0.5f), 1.0f, 0.0f));  // no bounce
    (void)floor;
    for (int i = 0; i < 1200; ++i) w.step(1.0 / 60.0);
    // Floor top at y = 9.5, ball radius 0.5 ⇒ resting center ≈ 9.0.
    CHECK(approx(w.body(ball).pos.y, 9.0f, 0.05f));
    CHECK(std::fabs(w.body(ball).vel.y) < 0.5f);       // came to rest, didn't tunnel
}

static void test_bouncy_ball_settles() {
    // Even with restitution > 0, the restitution-slop threshold must let a ball come
    // to rest on a floor instead of jittering forever.
    phys::World w;
    w.set_gravity({0, 20.0f});
    w.add(phys::static_body({0, 10}, phys::box({50, 0.5f}), 0.5f));
    const int ball = w.add(phys::make_body({0, 0}, phys::circle(0.5f), 1.0f, 0.5f));
    for (int i = 0; i < 1800; ++i) w.step(1.0 / 60.0);
    CHECK(approx(w.body(ball).pos.y, 9.0f, 0.1f));
    CHECK(std::fabs(w.body(ball).vel.y) < 0.5f);       // settled, not perpetually bouncing
}

int main() {
    test_detect();
    test_integration();
    test_static_immovable();
    test_elastic_exchange();
    test_inelastic();
    test_ball_settles_on_floor();
    test_bouncy_ball_settles();
    if (g_failures == 0) std::printf("physics: all tests passed\n");
    else                 std::printf("physics: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
