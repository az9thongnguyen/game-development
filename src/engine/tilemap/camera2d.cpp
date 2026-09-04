// =============================================================================
//  engine/tilemap/camera2d.cpp
// =============================================================================
#include "engine/tilemap/camera2d.hpp"

#include <cmath>

namespace tilemap {
namespace {
float roundf_(float v) { return std::floor(v + 0.5f); }
} // namespace

void Camera2D::clamp() {
    if (!has_bounds_) return;
    const float hw = static_cast<float>(vw_) * 0.5f;
    const float hh = static_cast<float>(vh_) * 0.5f;

    // A world narrower than the viewport is centred. Clamping it instead would pin
    // it to one edge and leave all the empty space on the other, which reads as a
    // bug even though every inequality holds.
    if (bw_ <= static_cast<float>(vw_)) pos_.x = bw_ * 0.5f;
    else if (pos_.x < hw)               pos_.x = hw;
    else if (pos_.x > bw_ - hw)         pos_.x = bw_ - hw;

    if (bh_ <= static_cast<float>(vh_)) pos_.y = bh_ * 0.5f;
    else if (pos_.y < hh)               pos_.y = hh;
    else if (pos_.y > bh_ - hh)         pos_.y = bh_ - hh;
}

void Camera2D::snap_to(Vec2f world) {
    pos_ = world;
    clamp();
}

void Camera2D::follow(Vec2f target, float dt) {
    // The deadzone is applied to the TARGET, not to the camera: work out the nearest
    // point that would put the target back inside the box, then move toward that.
    Vec2f want = pos_;
    const float dx = target.x - pos_.x;
    const float dy = target.y - pos_.y;
    if (dx >  dz_x_) want.x = target.x - dz_x_;
    if (dx < -dz_x_) want.x = target.x + dz_x_;
    if (dy >  dz_y_) want.y = target.y - dz_y_;
    if (dy < -dz_y_) want.y = target.y + dz_y_;

    if (smooth_ >= 1.0f || dt <= 0.0f) {
        pos_ = want;
    } else {
        // Framerate-independent exponential approach. Lerping by a constant per
        // FRAME makes the camera faster on a faster machine, which is the classic
        // way a game feels different depending on the hardware it runs on.
        const float t = 1.0f - std::pow(1.0f - smooth_, dt);
        pos_.x += (want.x - pos_.x) * t;
        pos_.y += (want.y - pos_.y) * t;
    }
    clamp();
}

Vec2f Camera2D::origin() const {
    return Vec2f{roundf_(pos_.x - static_cast<float>(vw_) * 0.5f),
                 roundf_(pos_.y - static_cast<float>(vh_) * 0.5f)};
}

Vec2f Camera2D::world_to_screen(Vec2f w) const {
    const Vec2f o = origin();
    return Vec2f{w.x - o.x, w.y - o.y};
}

Vec2f Camera2D::screen_to_world(Vec2f s) const {
    const Vec2f o = origin();
    return Vec2f{s.x + o.x, s.y + o.y};
}

void Camera2D::visible_tiles(int tile_px, int& x0, int& y0, int& x1, int& y1, int pad) const {
    if (tile_px <= 0) { x0 = y0 = 0; x1 = y1 = -1; return; }
    const Vec2f o = origin();
    x0 = static_cast<int>(std::floor(o.x / tile_px)) - pad;
    y0 = static_cast<int>(std::floor(o.y / tile_px)) - pad;
    x1 = static_cast<int>(std::floor((o.x + vw_ - 1) / tile_px)) + pad;
    y1 = static_cast<int>(std::floor((o.y + vh_ - 1) / tile_px)) + pad;
}

} // namespace tilemap
