// =============================================================================
//  engine/anim/flipbook.cpp
// =============================================================================
#include "engine/anim/flipbook.hpp"

namespace anim {

void Flipbook::update(float dt) {
    t += dt;
    if (loop && fps > 0.0f && frames > 0) {
        const float period = static_cast<float>(frames) / fps;   // seconds for one full cycle
        if (period > 0.0f) while (t >= period) t -= period;      // keep t bounded → no float drift
    }
}

int Flipbook::frame() const {
    if (frames <= 1) return 0;
    int f = fps > 0.0f ? static_cast<int>(t * fps) : 0;
    if (loop) { f %= frames; if (f < 0) f += frames; return f; }
    return f < 0 ? 0 : (f >= frames ? frames - 1 : f);           // one-shot: clamp + hold
}

bool Flipbook::done() const {
    return !loop && frames > 0 && fps > 0.0f && t * fps >= static_cast<float>(frames);
}

int frames_in_sheet(int w, int h) {
    if (w > 0 && h > w && h % w == 0) return h / w;
    return 1;
}

} // namespace anim
