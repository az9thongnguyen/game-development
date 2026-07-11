// =============================================================================
//  games/studio/sheet.hpp  —  build an animated sprite sheet from a recipe
// =============================================================================
//  A Texture Lab recipe makes one seamless, tileable texture. This turns it into
//  an N-frame animation for free: because the texture wraps, scrolling it by a
//  fraction of its width each frame produces a smooth, looping "flow" with no new
//  generator code. Frames are stacked vertically and kept SQUARE, so the sheet's
//  shape encodes its length (frames = height/width — see anim::frames_in_sheet).
//  See docs/book/88-studio-sheet-export.md.
// =============================================================================
#pragma once
#include "engine/image.hpp"
#include "games/studio/texture_gen.hpp"

namespace studio {

// Vertical N-frame animated sheet: frame f is the generated texture scrolled
// horizontally by f/N of its width (seamless because it's tileable). Result is
// size × (frames·size). frames<=1 returns the plain texture unchanged.
gfx::Image make_sheet(const TextureParams& p, int frames);

} // namespace studio
