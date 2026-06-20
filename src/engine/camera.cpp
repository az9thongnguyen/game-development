// =============================================================================
//  engine/camera.cpp  —  orbit + fly camera implementations
// =============================================================================
#include "engine/camera.hpp"

#include <cmath>

namespace cam {

namespace {
const math::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
const float kPitchLimit = math::radians(89.0f);  // never look exactly along ±Y
} // namespace

// ---- OrbitCamera ------------------------------------------------------------
void OrbitCamera::orbit(float dyaw, float dpitch) {
    yaw += dyaw;
    pitch = math::clampf(pitch + dpitch, -kPitchLimit, kPitchLimit);
}

void OrbitCamera::zoom(float factor) {
    distance = math::clampf(distance * factor, min_distance, max_distance);
}

math::vec3 OrbitCamera::eye() const {
    // Spherical -> cartesian offset from the target. yaw=0,pitch=0 sits on +Z,
    // so the camera looks down -Z at the target (matches the engine convention).
    const float cp = std::cos(pitch);
    const math::vec3 dir{cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)};
    return target + dir * distance;
}

math::mat4 OrbitCamera::view() const { return math::mat4_look_at(eye(), target, kWorldUp); }
math::mat4 OrbitCamera::proj(float aspect) const {
    return math::mat4_perspective(fovy, aspect, near_z, far_z);
}

// ---- FlyCamera --------------------------------------------------------------
math::vec3 FlyCamera::forward() const {
    const float cp = std::cos(pitch);
    // yaw=0,pitch=0 -> (0,0,-1): looking down -Z.
    return math::normalize(math::vec3{-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp});
}

math::vec3 FlyCamera::right() const { return math::normalize(math::cross(forward(), kWorldUp)); }

void FlyCamera::look(float dyaw, float dpitch) {
    yaw += dyaw;
    pitch = math::clampf(pitch + dpitch, -kPitchLimit, kPitchLimit);
}

void FlyCamera::move(float fwd, float strafe, float up) {
    pos = pos + forward() * fwd + right() * strafe + kWorldUp * up;
}

math::mat4 FlyCamera::view() const { return math::mat4_look_at(pos, pos + forward(), kWorldUp); }
math::mat4 FlyCamera::proj(float aspect) const {
    return math::mat4_perspective(fovy, aspect, near_z, far_z);
}

} // namespace cam
