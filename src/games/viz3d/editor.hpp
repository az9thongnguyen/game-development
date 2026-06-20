// =============================================================================
//  games/viz3d/editor.hpp  —  the interactive scene MODEL (pure, no SDL/renderer)
// =============================================================================
//  The editor's data: a list of placeable objects + which one is selected, plus
//  the verbs that change them (spawn / remove / select / cycle / pick). It knows
//  nothing about input or rendering — EditorScene drives it — so it is fully
//  unit-testable (tests/test_viz3d.cpp).
// =============================================================================
#pragma once

#include <vector>

#include "engine/color.hpp"
#include "engine/math.hpp"

namespace viz3d {

enum class Shape { Cube, Sphere, Plane, Cylinder };

// Canonical base dimensions — the single source of truth shared by the mesh
// templates (EditorScene builds meshes at these sizes) and bounding-sphere
// picking (Object::bound_radius below). Keep them in sync by using these.
inline constexpr float kCubeSize     = 1.6f;
inline constexpr float kSphereRadius = 1.0f;
inline constexpr float kPlaneSize    = 2.0f;
inline constexpr float kCylRadius    = 0.8f;
inline constexpr float kCylHeight    = 1.6f;

struct Object {
    Shape      shape = Shape::Cube;
    math::vec3 pos{};
    float      yaw   = 0.0f;
    float      pitch = 0.0f;
    float      scale = 1.0f;
    gfx::Color color = gfx::colors::white;

    math::mat4 model() const;        // T · Ry · Rx · S
    float      bound_radius() const; // world-space bounding sphere radius (for picking)
};

class Editor {
public:
    int  spawn(Shape shape, math::vec3 at, gfx::Color color);  // append + select; returns index
    void remove_selected();                                    // delete + deselect
    void select(int index);                                    // out-of-range → deselect
    void cycle();                                              // Tab: select next (wraps)

    // Nearest object hit by ray (origin ro, unit dir rd), or -1 on a miss.
    int  pick(math::vec3 ro, math::vec3 rd) const;

    int            selected_index() const { return selected_; }
    Object*        selected();
    const Object*  selected() const;
    const std::vector<Object>& objects() const { return objects_; }

private:
    std::vector<Object> objects_;
    int                 selected_ = -1;
};

} // namespace viz3d
