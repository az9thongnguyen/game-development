// =============================================================================
//  engine/anim/tween.hpp  —  easing curves + a deterministic scalar Tween
// =============================================================================
//  The interpolation primitive the rest of the engine animates on top of: a
//  catalog of named easing curves and a `Tween` that advances a value from →to
//  over a duration along one of them. Pure: no SDL, no IO, no wall clock — same
//  dt sequence reproduces the same values, so it unit-tests headless and any
//  subsystem (camera, UI, particles, sprite flipbooks) can own one.
//  See docs/book/83-tween-easing.md.
// =============================================================================
#pragma once

namespace anim {

// Named easing curves. t is progress in [0,1]; ease() returns eased progress,
// also normally in [0,1] — except Back/Elastic, which overshoot on purpose.
// Every curve satisfies f(0)=0 and f(1)=1.
enum class Ease {
    Linear,
    SmoothStep,
    QuadIn,    QuadOut,    QuadInOut,
    CubicIn,   CubicOut,   CubicInOut,
    SineInOut,
    ExpoOut,
    BackOut,
    ElasticOut,
    BounceOut,
};

// Evaluate an easing curve. `t` is clamped to [0,1] on entry, so callers can
// pass a raw (possibly out-of-range) progress without guarding.
float ease(Ease e, float t);

// Straight linear interpolation. No clamp — pair it with an eased t.
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// A one-shot (or ping-pong) scalar animation. Advance it with update(dt) and
// read value(); deterministic given the dt stream.
struct Tween {
    float from = 0, to = 1, dur = 1;      // start, end, seconds
    Ease  ease_fn = Ease::Linear;
    bool  pingpong = false;               // reverse at each end and run forever

    float elapsed  = 0;                   // seconds into the current leg
    bool  reversing = false;              // ping-pong phase (to→from)

    // Eased current value: lerp(from, to, ease(ease_fn, progress)).
    float value() const;
    // Advance time. One-shot: saturates at dur. Ping-pong: bounces, no drift.
    void  update(float dt);
    // One-shot completion (ping-pong tweens never finish).
    bool  done() const;
    void  reset() { elapsed = 0; reversing = false; }
};

} // namespace anim
