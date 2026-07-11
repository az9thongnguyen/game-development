// =============================================================================
//  tests/test_particles.cpp  —  particle sim core (dependency-free, CTest)
// =============================================================================
//  Verifies the pure simulation: burst count + spawn position, lifetime expiry,
//  seeded determinism, gravity, the bounded pool cap, and the fade helpers. No
//  Renderer2D / SDL / window.
// =============================================================================
#include "engine/fx/particles.hpp"

#include <algorithm>
#include <cstdio>

using namespace fx;

static int g_failures = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_failures; } \
    } while (0)

int main() {
    // burst: n at once, at the emit point
    {
        ParticleSystem s;
        s.emit_burst(50, 10, 20);
        CHECK(s.alive() == 50);
        CHECK(s.particles()[0].x == 10 && s.particles()[0].y == 20);
    }
    // lifetime: no emission; step well past max life -> all expire
    {
        EmitterConfig c; c.gravity = 0;
        ParticleSystem s; s.set_config(c);
        s.emit_burst(30, 0, 0);
        for (int i = 0; i < 200; ++i) s.update(0.02f, 0, 0, false);  // 4s > life+var
        CHECK(s.alive() == 0);
    }
    // determinism: same seed + identical calls -> identical particles
    {
        ParticleSystem a(7), b(7);
        for (int i = 0; i < 60; ++i) { a.update(0.016f, 5, 5, true); b.update(0.016f, 5, 5, true); }
        CHECK(a.alive() == b.alive() && a.alive() > 0);
        bool same = true;
        for (std::size_t i = 0; i < a.alive(); ++i)
            if (a.particles()[i].x != b.particles()[i].x || a.particles()[i].y != b.particles()[i].y)
                same = false;
        CHECK(same);
    }
    // gravity: one particle fired straight up rises then falls
    {
        EmitterConfig c; c.rate = 0; c.spread = 0; c.speed = 50; c.speed_var = 0;
        c.life = 5; c.life_var = 0; c.dir = -1.5708f;
        ParticleSystem s; s.set_config(c);
        s.emit_burst(1, 0, 0);
        float ymin = 0;
        for (int i = 0; i < 120; ++i) { s.update(0.016f, 0, 0, false); if (s.alive()) ymin = std::min(ymin, s.particles()[0].y); }
        CHECK(s.alive() == 1);
        CHECK(s.particles()[0].vy > 0);                 // now moving down
        CHECK(s.particles()[0].y > ymin);               // fell back below its apex
    }
    // cap: bursting past max never exceeds max
    {
        ParticleSystem s(1, 100);
        s.emit_burst(500, 0, 0);
        CHECK(s.alive() == 100);
    }
    // fade helpers: alpha full at birth, zero at death; size interpolates
    {
        Particle p; p.life = 1;
        p.age = 0; CHECK(gfx::a_of(current_color(p)) == 255);
        p.age = 1; CHECK(gfx::a_of(current_color(p)) == 0);
        p.size0 = 8; p.size1 = 0;
        p.age = 0; CHECK(current_size(p) == 8);
        p.age = 1; CHECK(current_size(p) == 0);
    }

    if (g_failures == 0) std::printf("particles: all tests passed\n");
    else                 std::printf("particles: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
