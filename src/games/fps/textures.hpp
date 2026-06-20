// =============================================================================
//  games/fps/textures.hpp  —  procedural wall textures (self-made, no assets)
// =============================================================================
//  Wall textures are generated in code (deterministic patterns) rather than
//  downloaded — simplest + fully "hand-made", and they reuse gfx::Image so the
//  raycaster samples them exactly like any loaded image. Indexed by wall id.
// =============================================================================
#pragma once

#include <array>
#include <cstdint>

#include "engine/image.hpp"

namespace fps {

struct WallTextures {
    std::array<gfx::Image, 4> tex;  // [0] unused; [1..3] = wall ids

    const gfx::Image& for_id(uint8_t id) const {
        return (id < tex.size() && !tex[id].pixels.empty()) ? tex[id] : tex[1];
    }
};

WallTextures make_wall_textures();  // 64x64 each: stone / brick / wood

gfx::Image make_barrel();  // 64x64 billboard sprite (transparent background)

} // namespace fps
