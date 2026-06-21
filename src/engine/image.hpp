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

// Load an .hrt image via the asset seam. Returns nullopt if missing/malformed.
std::optional<Image> load_image(const std::string& path);

} // namespace gfx
