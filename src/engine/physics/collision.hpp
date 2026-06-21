// =============================================================================
//  engine/physics/collision.hpp  —  narrowphase: do two bodies overlap, and how?
// =============================================================================
//  detect() returns a Manifold: whether the pair touches, the contact NORMAL
//  (pointing from a toward b), and the PENETRATION depth (how far they overlap).
//  The world uses the normal to bounce them apart and the penetration to un-sink
//  them. Pure math over Body/Shape — no state, fully testable.
// =============================================================================
#pragma once

#include "engine/math.hpp"
#include "engine/physics/body.hpp"

namespace phys {

struct Manifold {
    bool       hit         = false;
    math::vec2 normal      = {0.0f, 0.0f};   // unit vector from a → b
    float      penetration = 0.0f;           // overlap depth (>= 0 on hit)
};

// Detect collision between a and b (dispatches on their shape types).
Manifold detect(const Body& a, const Body& b);

} // namespace phys
