// =============================================================================
//  games/viz3d/editor.cpp  —  editor model implementation
// =============================================================================
#include "games/viz3d/editor.hpp"

#include <cmath>

#include "engine/pick.hpp"

namespace viz3d {

math::mat4 Object::model() const {
    return math::mat4_translate(pos)
         * math::mat4_rotate_y(yaw)
         * math::mat4_rotate_x(pitch)
         * math::mat4_scale({scale, scale, scale});
}

float Object::bound_radius() const {
    float r = 1.0f;
    switch (shape) {
        case Shape::Cube:     r = kCubeSize * 0.5f * 1.7320508f; break;  // half space-diagonal
        case Shape::Sphere:   r = kSphereRadius;                 break;
        case Shape::Plane:    r = kPlaneSize * 0.5f * 1.4142136f; break; // half face-diagonal
        case Shape::Cylinder: r = std::sqrt(kCylRadius * kCylRadius +
                                            (kCylHeight * 0.5f) * (kCylHeight * 0.5f)); break;
    }
    return r * scale;
}

int Editor::spawn(Shape shape, math::vec3 at, gfx::Color color) {
    objects_.push_back(Object{shape, at, 0.0f, 0.0f, 1.0f, color});
    selected_ = static_cast<int>(objects_.size()) - 1;
    return selected_;
}

void Editor::remove_selected() {
    if (selected_ < 0 || selected_ >= static_cast<int>(objects_.size())) return;
    objects_.erase(objects_.begin() + selected_);
    selected_ = -1;  // deselect (indices after the removed one have shifted)
}

void Editor::select(int index) {
    selected_ = (index >= 0 && index < static_cast<int>(objects_.size())) ? index : -1;
}

void Editor::cycle() {
    if (objects_.empty()) { selected_ = -1; return; }
    selected_ = (selected_ + 1) % static_cast<int>(objects_.size());
}

int Editor::pick(math::vec3 ro, math::vec3 rd) const {
    int best = -1;
    float best_t = 1e30f;
    for (int i = 0; i < static_cast<int>(objects_.size()); ++i) {
        const float t = pick::ray_sphere(ro, rd, objects_[i].pos, objects_[i].bound_radius());
        if (t >= 0.0f && t < best_t) { best_t = t; best = i; }
    }
    return best;
}

Object* Editor::selected() {
    return (selected_ >= 0 && selected_ < static_cast<int>(objects_.size())) ? &objects_[selected_]
                                                                             : nullptr;
}
const Object* Editor::selected() const {
    return (selected_ >= 0 && selected_ < static_cast<int>(objects_.size())) ? &objects_[selected_]
                                                                             : nullptr;
}

} // namespace viz3d
