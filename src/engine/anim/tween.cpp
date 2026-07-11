// =============================================================================
//  engine/anim/tween.cpp
// =============================================================================
#include "engine/anim/tween.hpp"

#include <cmath>

namespace anim {

namespace {
constexpr float kPi = 3.14159265358979323846f;

float bounce_out(float t) {
    const float n1 = 7.5625f, d1 = 2.75f;
    if (t < 1.0f / d1)      { return n1 * t * t; }
    if (t < 2.0f / d1)      { t -= 1.5f  / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1)      { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    /* else */              { t -= 2.625f/ d1; return n1 * t * t + 0.984375f; }
}
} // namespace

float ease(Ease e, float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);               // clamp on entry
    switch (e) {
        case Ease::Linear:     return t;
        case Ease::SmoothStep: return t * t * (3.0f - 2.0f * t);
        case Ease::QuadIn:     return t * t;
        case Ease::QuadOut:    return t * (2.0f - t);
        case Ease::QuadInOut:  return t < 0.5f ? 2.0f * t * t
                                               : 1.0f - 0.5f * (float)std::pow(-2.0f * t + 2.0f, 2);
        case Ease::CubicIn:    return t * t * t;
        case Ease::CubicOut:   { const float u = 1.0f - t; return 1.0f - u * u * u; }
        case Ease::CubicInOut: return t < 0.5f ? 4.0f * t * t * t
                                               : 1.0f - 0.5f * (float)std::pow(-2.0f * t + 2.0f, 3);
        case Ease::SineInOut:  return -(std::cos(kPi * t) - 1.0f) * 0.5f;
        case Ease::ExpoOut:    return t >= 1.0f ? 1.0f : 1.0f - (float)std::pow(2.0f, -10.0f * t);
        case Ease::BackOut: {
            const float c1 = 1.70158f, c3 = c1 + 1.0f, u = t - 1.0f;
            return 1.0f + c3 * u * u * u + c1 * u * u;
        }
        case Ease::ElasticOut: {
            if (t <= 0.0f) return 0.0f;
            if (t >= 1.0f) return 1.0f;
            const float c4 = (2.0f * kPi) / 3.0f;
            return (float)std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
        }
        case Ease::BounceOut:  return bounce_out(t);
    }
    return t;                                        // unreachable; keeps compilers happy
}

float Tween::value() const {
    float p = dur > 0 ? elapsed / dur : 1.0f;
    if (p > 1.0f) p = 1.0f;
    if (reversing) p = 1.0f - p;
    return lerp(from, to, ease(ease_fn, p));
}

void Tween::update(float dt) {
    elapsed += dt;
    if (pingpong) {
        if (dur > 0) {
            while (elapsed >= dur) { elapsed -= dur; reversing = !reversing; }
        } else {
            elapsed = 0;                             // zero-duration ping-pong: stay put
        }
    } else if (elapsed > dur) {
        elapsed = dur;                               // one-shot: hold at the end
    }
}

bool Tween::done() const {
    return !pingpong && dur > 0 && elapsed >= dur;
}

} // namespace anim
