// =============================================================================
//  engine/camera.hpp  —  orbit + free/fly cameras (engine core)
// =============================================================================
//  A camera's whole job is to produce two matrices for the renderer:
//      view()         — moves the world in front of the camera (inverse of the
//                       camera's pose); built with math::mat4_look_at.
//      proj(aspect)   — the perspective projection; built with mat4_perspective.
//
//  These are pure MATH controllers: they hold state and expose verbs (orbit,
//  zoom, look, move). The scene decides which key/mouse motion calls which verb,
//  so the cameras stay input-source-agnostic and unit-testable.
//
//  Two flavors, matching requirements.md §8:
//    * OrbitCamera — rotates around a target point; for inspecting/visualizing.
//    * FlyCamera   — a first-person eye that walks and looks; for exploring.
// =============================================================================
#pragma once

#include "engine/math.hpp"

namespace cam {

// A world-space ray (used for mouse picking against the scene).
struct Ray {
    math::vec3 origin;
    math::vec3 dir;   // normalized
};

// Orbit around `target` at `distance`, parameterized by yaw (around +Y) and
// pitch (elevation). Pitch is clamped so the camera never flips over the poles.
class OrbitCamera {
public:
    math::vec3 target{0.0f, 0.0f, 0.0f};
    float distance = 6.0f;
    float yaw   = math::radians(35.0f);
    float pitch = math::radians(20.0f);
    float fovy  = math::radians(60.0f);
    float near_z = 0.1f;
    float far_z  = 100.0f;
    float min_distance = 1.5f;
    float max_distance = 40.0f;

    void orbit(float dyaw, float dpitch);  // add to yaw/pitch (pitch clamped)
    void zoom(float factor);               // distance *= factor (clamped)
    void pan(float dx, float dy);          // slide the target in the view plane

    math::vec3 eye() const;                 // world-space camera position
    math::vec3 forward() const;             // eye -> target, normalized
    math::vec3 right() const;               // camera right axis
    math::vec3 up() const;                  // camera up axis
    math::mat4 view() const;
    math::mat4 proj(float aspect) const;

    // Build a world ray through a normalized screen point (ndc in [-1,+1], y up).
    // Used for mouse picking; no matrix inverse needed — just the camera basis.
    Ray ray_through(float ndc_x, float ndc_y, float aspect) const;
};

// A first-person camera at `pos` looking along yaw/pitch. yaw = 0 looks down -Z.
class FlyCamera {
public:
    math::vec3 pos{0.0f, 1.5f, 6.0f};
    float yaw   = 0.0f;
    float pitch = 0.0f;
    float fovy  = math::radians(60.0f);
    float near_z = 0.1f;
    float far_z  = 100.0f;

    math::vec3 forward() const;
    math::vec3 right() const;

    void look(float dyaw, float dpitch);          // turn (pitch clamped)
    void move(float fwd, float strafe, float up);  // translate along local axes

    math::mat4 view() const;
    math::mat4 proj(float aspect) const;
};

} // namespace cam
