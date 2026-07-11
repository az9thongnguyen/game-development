// =============================================================================
//  games/maplab/edit.hpp  —  pure grid-edit ops for the Map/Level Lab
// =============================================================================
//  The Lab edits a fps::Map (the one shared tile-grid model). These ops are the
//  interesting logic — bounds-safe cell writes, rect fill, and a 4-connected
//  flood fill — kept pure (no UI, no IO) so they unit-test with no window.
// =============================================================================
#pragma once
#include <cstdint>

#include "games/fps/map.hpp"

namespace maplab {

void set_cell (fps::Map& m, int x, int y, uint8_t id);                  // bounds-safe
void fill_rect(fps::Map& m, int x0, int y0, int x1, int y1, uint8_t id); // inclusive, clamped
void flood_fill(fps::Map& m, int x, int y, uint8_t id);                 // 4-connected region replace

fps::Map bordered(int w, int h);   // floor(0) with a one-cell wall(1) border

} // namespace maplab
