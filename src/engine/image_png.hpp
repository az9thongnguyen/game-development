// =============================================================================
//  engine/image_png.hpp  —  reading a format we did not invent
// =============================================================================
//  `.hrt` is what this engine ships (magic, width, height, raw RGBA — chapter 24),
//  and that was the right call: a format you can read in ten lines is a format that
//  never becomes a mystery. But nothing in the world exports `.hrt`, and every
//  open-licence art pack is PNG. So the door has to open once.
//
//  It opens INWARD only. This decodes; it does not encode. The engine has no reason
//  to write a PNG — art leaves as `.hrt` — and an encoder needs a compressor, which
//  is a much larger problem with no consumer here.
//
//  Supported, because it is what real packs contain: bit depth 8, colour types 0
//  (grey), 2 (RGB), 3 (palette, with tRNS), 4 (grey+alpha), 6 (RGBA), all five
//  scanline filters, non-interlaced. Everything else is REFUSED with a reason —
//  16-bit channels and Adam7 interlacing say so by name rather than producing an
//  image that is subtly wrong.
//
//  PURE: bytes in, Image out. No I/O, so the whole decoder is unit-testable against
//  files a real encoder produced.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/image.hpp"

namespace gfx {

// Decode PNG bytes into an ARGB8888 Image. `why` (optional) receives the reason on
// failure — "the art did not load" is not something anyone can act on.
std::optional<Image> decode_png(const std::vector<std::uint8_t>& bytes,
                                std::string* why = nullptr);

} // namespace gfx
