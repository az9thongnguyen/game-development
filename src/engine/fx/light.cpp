// =============================================================================
//  engine/fx/light.cpp
// =============================================================================
#include "engine/fx/light.hpp"

#include <cmath>

namespace fx {

float light_falloff(float dist, float radius) {
    if (radius <= 0.0f) return 0.0f;
    const float x = dist / radius;
    if (x >= 1.0f) return 0.0f;
    const float k = 1.0f - x * x;
    return k * k;                              // 1 at x=0 → 0 at x=1, smooth
}

gfx::Color light_sample(const Light& L, float px, float py) {
    const float dx = px - L.x, dy = py - L.y;
    const float d  = std::sqrt(dx * dx + dy * dy);
    float f = L.intensity * light_falloff(d, L.radius);
    if (f <= 0.0f) return L.color & 0x00FFFFFFu;      // alpha 0 → adds nothing
    if (f > 1.0f) f = 1.0f;
    const auto a = static_cast<std::uint8_t>(f * 255.0f + 0.5f);
    return gfx::rgba(gfx::r_of(L.color), gfx::g_of(L.color), gfx::b_of(L.color), a);
}

} // namespace fx
