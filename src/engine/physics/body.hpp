// =============================================================================
//  engine/physics/body.hpp  —  a 2D rigid body (linear only)
// =============================================================================
//  Position + velocity, a shape, and INVERSE mass. Inverse mass (1/m) is what the
//  impulse math actually uses, and it makes "static / immovable" trivial: inv_mass
//  == 0 means infinite mass (a floor, a wall) that never accelerates. No rotation in
//  this version (no angular velocity / inertia) — that keeps the math correct and
//  readable; rotation is a documented extension.
// =============================================================================
#pragma once

#include "engine/math.hpp"
#include "engine/physics/shapes.hpp"

namespace phys {

struct Body {
    math::vec2 pos{0.0f, 0.0f};
    math::vec2 vel{0.0f, 0.0f};
    float      inv_mass    = 1.0f;     // 0 => static (infinite mass, immovable)
    float      restitution = 0.2f;     // bounciness: 0 = dead, 1 = perfectly elastic
    Shape      shape       = circle(0.5f);

    bool dynamic() const { return inv_mass > 0.0f; }
};

// Convenience constructors.
inline Body make_body(math::vec2 pos, Shape shape, float mass = 1.0f, float restitution = 0.2f) {
    Body b;
    b.pos         = pos;
    b.shape       = shape;
    b.inv_mass    = (mass > 0.0f) ? 1.0f / mass : 0.0f;
    b.restitution = restitution;
    return b;
}
inline Body static_body(math::vec2 pos, Shape shape, float restitution = 0.2f) {
    Body b;
    b.pos         = pos;
    b.shape       = shape;
    b.inv_mass    = 0.0f;
    b.restitution = restitution;
    return b;
}

} // namespace phys
