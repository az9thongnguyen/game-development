// =============================================================================
//  games/iso/iso_render.hpp  —  draw a Farm into the 2D framebuffer
// =============================================================================
//  The ONLY iso file that touches the engine renderer. It reads a Farm (never
//  mutates it) and paints: ground tiles back-to-front, then objects + the farmer
//  depth-sorted by their iso key (painter's algorithm), then a hover highlight.
//  No SDL — everything goes through Renderer2D, like every other scene.
// =============================================================================
#pragma once

#include "engine/iso.hpp"
#include "engine/renderer2d.hpp"
#include "games/iso/farm.hpp"

namespace iso {

// The 2D camera is just the screen pixel where grid cell (0,0) is centered.
// Panning moves this offset; there is no zoom in M4 (an exercise).
struct Camera2D {
    float ox = 480.0f;
    float oy = 80.0f;
};

// Draw the farm. `hovered` is the grid cell under the cursor (may be out of
// bounds — then no highlight is drawn). The HUD text is the scene's job.
void render_farm(gfx::Renderer2D& g, const Farm& f, const Camera2D& cam, Vec2i hovered);

} // namespace iso
