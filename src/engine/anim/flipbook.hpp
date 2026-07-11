// =============================================================================
//  engine/anim/flipbook.hpp  —  frame-index playback for sprite-sheet animation
// =============================================================================
//  A flipbook advances a frame index through a sprite sheet at a fixed rate.
//  Pure and deterministic (no clock — you feed it dt), so it unit-tests headless
//  and any scene can own one. It knows nothing about pixels: it just answers
//  "which frame now?"; the scene slices that frame out of a (vertically packed)
//  sheet image and blits it. Pairs with the Tween in tween.hpp — same anim lib.
//  See docs/book/86-sprite-animation.md.
// =============================================================================
#pragma once

namespace anim {

// Plays frame indices 0..frames-1 at `fps`, looping or one-shot.
struct Flipbook {
    int   frames = 1;      // number of frames in the sheet
    float fps    = 8.0f;   // playback rate (frames per second)
    bool  loop   = true;   // wrap forever, or stop (and hold) on the last frame
    float t      = 0;      // seconds elapsed in the current cycle

    void update(float dt);   // advance; loop keeps t bounded to one period
    int  frame() const;      // current frame index, always in [0, frames-1]
    bool done()  const;      // one-shot only: true once past the last frame
    void reset() { t = 0; }
};

// Frame count of a vertically-packed sprite sheet with SQUARE frames: a sheet of N
// w×w frames is w×(N·w), so N = h/w. Any image that isn't a taller-than-wide exact
// multiple is a single static frame. This lets a sheet self-describe its length by
// its shape — no sidecar, no filename convention.
int frames_in_sheet(int w, int h);

} // namespace anim
