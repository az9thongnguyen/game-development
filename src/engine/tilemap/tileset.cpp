// =============================================================================
//  engine/tilemap/tileset.cpp  —  see tileset.hpp
// =============================================================================
#include "engine/tilemap/tileset.hpp"

namespace tilemap {

Tileset Tileset::cut(const gfx::Image& sheet, int tile) {
    Tileset out;
    if (tile <= 0 || sheet.w < tile || sheet.h < tile) return out;   // nothing to cut
    out.tile_ = tile;
    out.cols_ = sheet.w / tile;
    const int rows = sheet.h / tile;

    out.tiles_.reserve(static_cast<std::size_t>(out.cols_) * static_cast<std::size_t>(rows));
    for (int ty = 0; ty < rows; ++ty) {
        for (int tx = 0; tx < out.cols_; ++tx) {
            gfx::Image cell;
            cell.w = tile;
            cell.h = tile;
            cell.pixels.resize(static_cast<std::size_t>(tile) * static_cast<std::size_t>(tile));
            for (int y = 0; y < tile; ++y) {
                const std::size_t src = static_cast<std::size_t>(ty * tile + y) *
                                            static_cast<std::size_t>(sheet.w) +
                                        static_cast<std::size_t>(tx * tile);
                const std::size_t dst = static_cast<std::size_t>(y) * static_cast<std::size_t>(tile);
                for (int x = 0; x < tile; ++x)
                    cell.pixels[dst + static_cast<std::size_t>(x)] =
                        sheet.pixels[src + static_cast<std::size_t>(x)];
            }
            out.tiles_.push_back(std::move(cell));
        }
    }
    return out;
}

gfx::Sprite Tileset::sprite(std::size_t index) const {
    if (index >= tiles_.size()) return gfx::Sprite{nullptr, 0, 0};
    const gfx::Image& t = tiles_[index];
    return gfx::Sprite{t.pixels.data(), t.w, t.h};
}

AnimatedTileset AnimatedTileset::cut(const gfx::Image& sheet, int tile, float fps) {
    AnimatedTileset out;
    out.tiles_ = Tileset::cut(sheet, tile);
    out.playback_.frames = anim::frames_in_sheet(sheet.w, sheet.h);
    out.playback_.fps = fps;
    const std::size_t frames = static_cast<std::size_t>(out.playback_.frames);
    out.tiles_per_frame_ = frames > 0 ? out.tiles_.count() / frames : 0;
    return out;
}

gfx::Sprite AnimatedTileset::sprite(std::size_t index) const {
    if (index >= tiles_per_frame_) return gfx::Sprite{nullptr, 0, 0};
    const std::size_t frame = static_cast<std::size_t>(playback_.frame());
    return tiles_.sprite(frame * tiles_per_frame_ + index);
}

} // namespace tilemap
