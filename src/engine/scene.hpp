// =============================================================================
//  engine/scene.hpp  —  the Scene interface + per-frame Context
// =============================================================================
//  A "scene" is one screen of the game: the M0 demo, the chess board, a menu.
//  The engine drives scenes through two methods:
//
//      update(dt)  — advance game logic by a FIXED time step (see app.cpp).
//                    Called zero or more times per rendered frame.
//      render(ctx) — draw the current state into the framebuffer.
//                    Called exactly once per rendered frame.
//
//  Splitting "advance the simulation" from "draw it" is what lets the update rate
//  stay constant (deterministic physics/AI) while the render rate floats with the
//  display. Chapter 03 explains why this matters.
// =============================================================================
#pragma once

#include "engine/renderer2d.hpp"

namespace engine {

// Everything a scene needs to draw one frame. Passed by const ref to render().
struct Context {
    gfx::Renderer2D&            gfx;    // draw API over this frame's framebuffer
    const platform::InputState& input;  // normalized keyboard/mouse this frame
    double                      dt;     // real seconds since the last rendered frame
    double                      time;   // total simulated seconds (sum of fixed steps)
    double                      alpha;  // [0,1): fraction into the next fixed step
};

class Scene {
public:
    virtual ~Scene() = default;

    // Fixed-timestep logic with the current input. Default: do nothing.
    virtual void update(double dt, const platform::InputState& input) {
        (void)dt;
        (void)input;
    }

    // Draw one frame. Required.
    virtual void render(const Context& ctx) = 0;
};

} // namespace engine
