// =============================================================================
//  engine/tilemap/autotile.hpp  —  the 47-tile "blob" rule
// =============================================================================
//  Painting a grass patch by hand means choosing the right edge and corner piece for
//  every cell and redoing it whenever a neighbour changes. Autotiling makes that the
//  computer's job: given which of the eight neighbours are the same material, pick
//  the piece that fits.
//
//  Eight neighbours is 256 combinations, but only 47 are distinct pieces. The
//  reduction is the whole idea: a DIAGONAL neighbour only matters when both of the
//  cardinals beside it are also filled. A corner tile that meets nothing along its
//  edges has nothing to blend into, so the diagonal cannot change what the piece
//  looks like. Folding those away collapses 256 down to 47.
//
//  PURE: one integer in, one integer out, no state and no I/O. There is deliberately
//  no tileset FILE format here yet — the artwork mapping and the editor that
//  authors it arrive together with the editor (see the chapter). This is the part
//  with the actual algorithm in it.
// =============================================================================
#pragma once

#include <cstdint>

namespace tilemap {

// Neighbour bits, clockwise from north. Pass the OR of the neighbours that hold the
// SAME material as the cell being resolved.
enum : std::uint8_t {
    kN  = 1u << 0,
    kNE = 1u << 1,
    kE  = 1u << 2,
    kSE = 1u << 3,
    kS  = 1u << 4,
    kSW = 1u << 5,
    kW  = 1u << 6,
    kNW = 1u << 7,
};

// Drop every diagonal whose two adjacent cardinals are not both set. Two masks that
// differ only in such a diagonal describe the same piece.
std::uint8_t autotile_canonical(std::uint8_t mask);

// Index of the piece for `mask`, in [0,47). Stable: the same mask always maps to the
// same index, and canonically-equal masks map to the same index.
int autotile_index(std::uint8_t mask);

// How many distinct pieces exist. 47, by construction — asserted by the tests
// rather than hard-coded as folklore.
int autotile_count();

} // namespace tilemap
