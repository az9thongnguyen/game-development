// =============================================================================
//  engine/fx/light.hpp  —  2D radial lights (pure math; drawing is scene-side)
// =============================================================================
//  A "light" is a soft radial glow: full brightness at its centre, smoothly
//  falling to nothing at its radius. This header owns only the *math* — the
//  falloff curve and the per-pixel additive contribution — so it unit-tests
//  headless. The actual framebuffer deposit is a scene-side loop over
//  Renderer2D::add_pixel (the additive counterpart of blend_pixel), exactly the
//  pure-core / scene-glue split the particle system uses.
//  See docs/book/84-2d-lighting.md.
// =============================================================================
#pragma once

#include "engine/color.hpp"

namespace fx {

struct Light {
    float      x = 0, y = 0;                 // centre, logical pixels
    float      radius = 120;                 // reach; contribution is 0 beyond it
    gfx::Color color = gfx::rgb(255, 240, 200);
    float      intensity = 1.0f;             // overall multiplier (can exceed 1 to blow out)
};

// Smooth radial falloff: 1 at the centre, 0 at (and beyond) `radius`, monotonically
// decreasing in between. Uses (1 - (d/r)^2)^2 — cheap, soft-edged, no sqrt needed
// by callers that already have squared distance can't use this directly (pass d).
float light_falloff(float dist, float radius);

// The additive colour a light deposits at pixel (px,py): the light's RGB with
// alpha = clamp(intensity * falloff, 0..1) scaled to 0..255. Renderer2D::add_pixel
// reads that alpha as the additive weight. Alpha 0 → nothing to add.
gfx::Color light_sample(const Light& L, float px, float py);

} // namespace fx
