// =============================================================================
//  engine/pick.hpp  —  ray intersection helpers for mouse picking (pure)
// =============================================================================
//  Header-only, no SDL: turn a camera ray (Chapter 21's OrbitCamera::ray_through)
//  into "what did the user click on?" and "where on the ground is the cursor?".
//  Unit-tested in tests/test_viz3d.cpp.
// =============================================================================
#pragma once

#include <cmath>

#include "engine/math.hpp"

namespace pick {

// Nearest positive distance t at which ray (origin o, unit direction d) meets the
// sphere of center c and radius r, or -1 if it misses. If the origin is INSIDE the
// sphere, returns the far (exit) hit so a click from within still selects it.
inline float ray_sphere(math::vec3 o, math::vec3 d, math::vec3 c, float r) {
    const math::vec3 oc = o - c;
    const float b = math::dot(oc, d);         // d is unit ⇒ quadratic 'a' = 1
    const float cc = math::dot(oc, oc) - r * r;
    const float disc = b * b - cc;
    if (disc < 0.0f) return -1.0f;            // no real roots → miss
    const float s = std::sqrt(disc);
    float t = -b - s;                          // near root
    if (t < 0.0f) t = -b + s;                  // origin inside → far root
    return t >= 0.0f ? t : -1.0f;
}

// Intersect ray (o, d) with the horizontal plane y = py. Returns false if the ray
// is parallel to the plane or the hit is behind the origin.
inline bool ray_plane_y(math::vec3 o, math::vec3 d, float py, math::vec3& out) {
    if (std::fabs(d.y) < 1e-6f) return false;
    const float t = (py - o.y) / d.y;
    if (t < 0.0f) return false;
    out = o + d * t;
    return true;
}

} // namespace pick
