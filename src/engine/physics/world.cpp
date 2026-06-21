// =============================================================================
//  engine/physics/world.cpp  —  the simulation step
// =============================================================================
#include "engine/physics/world.hpp"

#include <algorithm>
#include <vector>

#include "engine/physics/collision.hpp"

namespace phys {

void World::step(double dt, int iterations) {
    const float h = static_cast<float>(dt);

    // 1) Integrate (semi-implicit Euler): velocity first, then position. Static
    //    bodies (inv_mass 0) don't move.
    for (Body& b : bodies_) {
        if (!b.dynamic()) continue;
        b.vel = b.vel + gravity_ * h;
        b.pos = b.pos + b.vel * h;
    }

    // 2) Broadphase + 3) narrowphase: O(n^2) pairs → contact manifolds. Skip pairs of
    //    two static bodies (they can't move anyway).
    struct Contact { int a, b; Manifold m; };
    std::vector<Contact> contacts;
    const int n = static_cast<int>(bodies_.size());
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (!bodies_[static_cast<std::size_t>(i)].dynamic() &&
                !bodies_[static_cast<std::size_t>(j)].dynamic())
                continue;
            const Manifold m = detect(bodies_[static_cast<std::size_t>(i)],
                                      bodies_[static_cast<std::size_t>(j)]);
            if (m.hit) contacts.push_back({i, j, m});
        }
    }

    // 4a) Resolve velocities with sequential impulses (a few passes for stability).
    for (int it = 0; it < iterations; ++it) {
        for (const Contact& c : contacts) {
            Body& A = bodies_[static_cast<std::size_t>(c.a)];
            Body& B = bodies_[static_cast<std::size_t>(c.b)];
            const float inv_sum = A.inv_mass + B.inv_mass;
            if (inv_sum <= 0.0f) continue;

            const math::vec2 rv = B.vel - A.vel;        // relative velocity
            const float      vn = math::dot(rv, c.m.normal);
            if (vn > 0.0f) continue;                    // already separating

            // Restitution slop: only bounce above a small approach speed. Below it
            // (a body resting under gravity) treat e as 0, so resting contacts don't
            // jitter forever as positional correction and restitution fight.
            constexpr float kRestThreshold = 1.0f;
            const float e  = (vn < -kRestThreshold) ? std::min(A.restitution, B.restitution) : 0.0f;
            const float j  = -(1.0f + e) * vn / inv_sum;
            const math::vec2 impulse = c.m.normal * j;
            A.vel = A.vel - impulse * A.inv_mass;
            B.vel = B.vel + impulse * B.inv_mass;
        }
    }

    // 4b) Positional correction (Baumgarte): nudge bodies apart so they don't sink
    //     into each other. `slop` ignores tiny overlaps; `percent` softens the push.
    constexpr float slop = 0.01f, percent = 0.2f;
    for (const Contact& c : contacts) {
        Body& A = bodies_[static_cast<std::size_t>(c.a)];
        Body& B = bodies_[static_cast<std::size_t>(c.b)];
        const float inv_sum = A.inv_mass + B.inv_mass;
        if (inv_sum <= 0.0f) continue;
        const float      corr = std::max(c.m.penetration - slop, 0.0f) / inv_sum * percent;
        const math::vec2 push = c.m.normal * corr;
        A.pos = A.pos - push * A.inv_mass;
        B.pos = B.pos + push * B.inv_mass;
    }
}

} // namespace phys
