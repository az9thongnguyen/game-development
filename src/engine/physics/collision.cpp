// =============================================================================
//  engine/physics/collision.cpp  —  narrowphase implementations
// =============================================================================
#include "engine/physics/collision.hpp"

#include <cmath>

namespace phys {
namespace {

// Two circles: hit when centers are closer than the radius sum.
Manifold circle_circle(const Body& a, const Body& b) {
    Manifold m;
    const math::vec2 d  = b.pos - a.pos;          // a → b
    const float      r  = a.shape.radius + b.shape.radius;
    const float      d2 = math::length2(d);
    if (d2 >= r * r) return m;
    const float dist = std::sqrt(d2);
    m.hit = true;
    if (dist > 1e-6f) { m.normal = d / dist; m.penetration = r - dist; }
    else              { m.normal = {1.0f, 0.0f}; m.penetration = r; }   // coincident centers
    return m;
}

// Two AABBs: overlap on both axes; separate along the axis of LEAST penetration.
Manifold box_box(const Body& a, const Body& b) {
    Manifold m;
    const math::vec2 d  = b.pos - a.pos;
    const float      ox = (a.shape.half.x + b.shape.half.x) - std::fabs(d.x);
    if (ox <= 0.0f) return m;
    const float      oy = (a.shape.half.y + b.shape.half.y) - std::fabs(d.y);
    if (oy <= 0.0f) return m;
    m.hit = true;
    if (ox < oy) { m.normal = {(d.x < 0.0f ? -1.0f : 1.0f), 0.0f}; m.penetration = ox; }
    else         { m.normal = {0.0f, (d.y < 0.0f ? -1.0f : 1.0f)}; m.penetration = oy; }
    return m;
}

// Circle `circ` vs box `bx`. Returns the manifold with normal pointing circ → bx.
Manifold circle_box(const Body& circ, const Body& bx) {
    Manifold        m;
    const math::vec2 h = bx.shape.half;
    const math::vec2 d = circ.pos - bx.pos;        // box-center → circle-center
    const math::vec2 closest{ math::clampf(d.x, -h.x, h.x), math::clampf(d.y, -h.y, h.y) };
    const bool       inside = (d.x >= -h.x && d.x <= h.x && d.y >= -h.y && d.y <= h.y);
    const float      r = circ.shape.radius;

    math::vec2 box_to_circle;   // separation direction (push circle away from box)
    float      penetration;
    if (inside) {
        // Center is inside the box: pop out along the nearest face.
        const float penx = h.x - std::fabs(d.x);
        const float peny = h.y - std::fabs(d.y);
        if (penx < peny) { box_to_circle = {(d.x < 0.0f ? -1.0f : 1.0f), 0.0f}; penetration = r + penx; }
        else             { box_to_circle = {0.0f, (d.y < 0.0f ? -1.0f : 1.0f)}; penetration = r + peny; }
        m.hit = true;
    } else {
        const math::vec2 to_c  = d - closest;       // box surface → circle center
        const float      dist2 = math::length2(to_c);
        if (dist2 >= r * r) return m;
        const float dist = std::sqrt(dist2);
        box_to_circle = (dist > 1e-6f) ? to_c / dist : math::vec2{0.0f, -1.0f};
        penetration   = r - dist;
        m.hit = true;
    }
    m.normal      = -box_to_circle;   // want circ → bx, i.e. opposite of box → circle
    m.penetration = penetration;
    return m;
}

} // namespace

Manifold detect(const Body& a, const Body& b) {
    const ShapeType ta = a.shape.type, tb = b.shape.type;
    if (ta == ShapeType::Circle && tb == ShapeType::Circle) return circle_circle(a, b);
    if (ta == ShapeType::Box    && tb == ShapeType::Box)    return box_box(a, b);
    if (ta == ShapeType::Circle && tb == ShapeType::Box)    return circle_box(a, b);
    // Box (a) vs Circle (b): compute circle→box for (b,a), then flip to get a → b.
    Manifold m = circle_box(b, a);
    m.normal = -m.normal;
    return m;
}

} // namespace phys
