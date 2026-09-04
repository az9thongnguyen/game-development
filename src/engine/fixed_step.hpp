// =============================================================================
//  engine/fixed_step.hpp  —  variable real time in, fixed logic steps out
// =============================================================================
//  This is the accumulator that used to live inside App::frame. It moved out for a
//  reason that only appeared when something else needed to drive a scene: App::frame
//  is welded to platform::framebuffer() and platform::input(), so it can run a scene
//  into the window and nowhere else. The Play viewport runs one into a buffer of its
//  own — and the ONE thing it must not do is write its own accumulator.
//
//  A second copy would not look wrong. It would clamp at a slightly different value,
//  or round the leftover differently, and the two would agree on every ordinary frame
//  and disagree the moment the machine hiccuped — which is exactly when a determinism
//  bug is least reproducible and most expensive.
//
//  PURE: arithmetic on doubles. No platform, no scene, no I/O.
// =============================================================================
#pragma once

namespace engine {

class FixedStep {
public:
    // `dt` is the simulation step; `max_frame` caps how much real time one frame may
    // contribute. Without that cap, a frame that took two seconds (window dragged,
    // breakpoint hit, laptop slept) asks for 120 updates at once, each of which makes
    // the next frame later — the "spiral of death". Clamping DROPS simulated time on
    // purpose: the world runs slow for one frame instead of freezing for several.
    explicit FixedStep(double dt = 1.0 / 60.0, double max_frame = 0.25)
        : dt_(dt), max_frame_(max_frame) {}

    // Feed one frame of real seconds; returns how many fixed updates to run NOW.
    // The caller loops that many times — it is not a coroutine and holds no callback,
    // which is what keeps it testable without a scene.
    int advance(double real_dt) {
        if (real_dt > max_frame_) real_dt = max_frame_;
        if (real_dt > 0.0) accumulator_ += real_dt;
        int steps = 0;
        while (accumulator_ >= dt_) {
            accumulator_ -= dt_;
            time_        += dt_;
            ++steps;
        }
        return steps;
    }

    [[nodiscard]] double dt() const { return dt_; }
    [[nodiscard]] double time() const { return time_; }

    // How far into the next not-yet-simulated step we are, in [0,1). A renderer uses
    // it to interpolate so motion looks smooth even though logic ticks at a fixed rate.
    [[nodiscard]] double alpha() const { return accumulator_ / dt_; }

    // Back to zero. A Play viewport that restarts a game must restart its clock too,
    // or the first frame after Restart carries the leftover of the run before it.
    void reset() { accumulator_ = 0.0; time_ = 0.0; }

private:
    double dt_;
    double max_frame_;
    double accumulator_ = 0.0;
    double time_        = 0.0;
};

} // namespace engine
