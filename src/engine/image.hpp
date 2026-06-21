// =============================================================================
//  engine/image.hpp  —  hand-written loader for our tiny ".hrt" raster format
// =============================================================================
//  Loads images without any third-party image library (we hand-parse the bytes),
//  per the project's thin-shim rule. The .hrt format is produced offline by
//  scripts/fetch_pieces.py:
//      magic "HRT1" | uint32 BE width | uint32 BE height | RGBA8 rows
//  We parse those bytes ourselves and hand back ARGB8888 pixels the renderer
//  can blit directly.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "engine/color.hpp"

namespace gfx {

struct Image {
    int                w = 0;
    int                h = 0;
    std::vector<Color> pixels;  // ARGB8888, row-major (w*h)
};

// Decode raw .hrt bytes into an Image (nullopt if malformed). Pure — no I/O — so it
// composes with the asset cache (which reads the bytes itself) and is unit-testable.
std::optional<Image> decode_hrt(const std::vector<uint8_t>& bytes);

// Load an .hrt image via the asset seam (reads the file, then decode_hrt).
std::optional<Image> load_image(const std::string& path);

} // namespace gfx
