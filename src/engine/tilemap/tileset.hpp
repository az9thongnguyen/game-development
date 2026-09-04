// =============================================================================
//  engine/tilemap/tileset.hpp  —  a sheet of tiles, cut once
// =============================================================================
//  An art pack ships one image with the tiles packed into a grid. The renderer
//  blits `gfx::Sprite`s, which are tightly packed — there is no stride — so the
//  sheet is cut into individual images once at load rather than sampled from with
//  an offset on every draw.
//
//  132 tiles of 16x16 is 135 KB. Cutting is a copy of the whole sheet, once, and it
//  buys a `sprite(id)` that costs nothing per frame.
//
//  PURE: an Image in, Images out. It does not know where the sheet came from — a
//  CC0 pack imported through `asset.import`, or something drawn in the Texture Lab.
//  That is the whole point of importing to one format.
// =============================================================================
#pragma once

#include <cstddef>
#include <vector>

#include "engine/image.hpp"
#include "engine/renderer2d.hpp"

namespace tilemap {

class Tileset {
public:
    // Cut `sheet` into `tile`x`tile` cells, left to right then top to bottom — the
    // order every packer uses and every editor numbers by. Cells that would fall off
    // the right or bottom edge of a sheet that is not an exact multiple are dropped
    // rather than padded: a half tile is not a tile.
    static Tileset cut(const gfx::Image& sheet, int tile);

    [[nodiscard]] std::size_t count() const { return tiles_.size(); }
    [[nodiscard]] int         tile()  const { return tile_; }
    [[nodiscard]] int         columns() const { return cols_; }

    // A blittable view of tile `index`, or a null sprite (w == 0) when the index is
    // out of range — so a caller can ask without checking twice and fall back to
    // whatever it drew before there was art.
    [[nodiscard]] gfx::Sprite sprite(std::size_t index) const;

private:
    std::vector<gfx::Image> tiles_;
    int                     tile_ = 0;
    int                     cols_ = 0;
};

} // namespace tilemap
