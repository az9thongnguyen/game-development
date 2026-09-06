// =============================================================================
//  engine/paint/colour.hpp  —  the two doors to a colour the image does not have
// =============================================================================
//  The pixel workspace could only ever select a colour the image ALREADY held: its
//  palette is sampled from the file (`build_palette`) and its eyedropper reads a
//  pixel. Both are the right defaults — matching a neighbour is most of pixel art —
//  and together they made the editor unable to introduce a single new hue. Chapter
//  125 went through the `.pix` door partly because of this.
//
//  Two doors, because they answer different questions:
//
//    HSV   — "a bit darker than that, same colour". A shade is one axis in HSV and
//            three correlated ones in RGB, so a value slider is the difference
//            between one gesture and three guesses. This is the EXPLORING door.
//    hex   — "#8B5A2B, the one the pack uses". A slider is a pixel-per-step drag
//            and cannot be told an exact triple; a hex code is how a colour is
//            written down and passed between people. This is the EXACT door.
//
//  PURE: no renderer, no UI, no I/O — arithmetic and a parser, so the whole thing is
//  checked by test_paint with no window.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "engine/color.hpp"

namespace paint {

// Hue in degrees [0,360), saturation and value in [0,1].
//
// A workspace holds one of these rather than a packed colour, and that is the whole
// reason the type exists: a colour cannot say where the sliders are. Every colour
// with v=0 is black, so deriving the sliders from the colour each frame would make
// a value drag to the bottom FORGET the hue — and dragging back up would return
// black, not the colour you started from. The mixer's state is the coordinates, not
// the result.
struct Hsv {
    float h = 0.0f, s = 0.0f, v = 0.0f;
};

// Alpha is not part of Hsv: it is carried alongside, unchanged by a hue drag.
Hsv        to_hsv(gfx::Color c);
gfx::Color from_hsv(Hsv c, std::uint8_t a = 255);

// "#AARRGGBB", upper case, always nine characters. Alpha first because that is the
// order the pixels are packed in (0xAARRGGBB) — matching CSS's #RRGGBBAA instead
// would put the display and the memory in two different orders for one value.
std::string to_hex(gfx::Color c);

// Accepts "#RGB", "#RRGGBB" and "#AARRGGBB", with or without the '#', in either
// case, with surrounding spaces. Anything else is nullopt rather than a guess: the
// caller is a text field being typed INTO, and half-typed input must leave the
// current colour alone instead of jumping to whatever the prefix parses as.
std::optional<gfx::Color> parse_hex(std::string_view s);

} // namespace paint
