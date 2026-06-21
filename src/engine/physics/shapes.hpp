// =============================================================================
//  engine/physics/shapes.hpp  —  collision shapes (Circle, axis-aligned Box)
// =============================================================================
//  Two shapes are enough to build a real 2D physics demo: a Circle (radius) and an
//  axis-aligned Box (half-extents). A Shape is a small tagged value — no inheritance,
//  no allocation — so a Body owns one by value.
// =============================================================================
#pragma once

#include "engine/math.hpp"

namespace phys {

enum class ShapeType { Circle, Box };

struct Shape {
    ShapeType  type   = ShapeType::Circle;
    float      radius = 0.5f;            // Circle
    math::vec2 half   = {0.5f, 0.5f};    // Box half-extents
};

inline Shape circle(float r)            { Shape s; s.type = ShapeType::Circle; s.radius = r; return s; }
inline Shape box(math::vec2 half)       { Shape s; s.type = ShapeType::Box;    s.half = half; return s; }

} // namespace phys
