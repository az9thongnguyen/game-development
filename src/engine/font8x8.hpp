// =============================================================================
//  engine/font8x8.hpp  —  embedded 8x8 bitmap font
// =============================================================================
//  Public-domain 8x8 monochrome font by Daniel Hepper, based on Marcel Sondaar /
//  IBM public-domain VGA fonts. Vendored (see font8x8.inc) so the build needs no
//  network or external asset.  License: Public Domain.
//
//  Layout: 128 glyphs (ASCII / basic latin). Each glyph is 8 bytes = 8 rows.
//  Within a row byte, bit 0 (the LEAST significant bit) is the LEFT-most pixel:
//
//      row byte 0x0C = 0b00001100  ->  ..##....   (columns 2 and 3 lit)
// =============================================================================
#pragma once

namespace gfx {

inline constexpr unsigned char kFont8x8[128][8] = {
#include "font8x8.inc"
};

} // namespace gfx
