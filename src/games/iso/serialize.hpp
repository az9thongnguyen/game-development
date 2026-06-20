// =============================================================================
//  games/iso/serialize.hpp  —  save / load a Farm to / from a byte buffer
// =============================================================================
//  We serialize to a small, versioned, HUMAN-READABLE text format (easy to read,
//  diff, and hand-edit while learning) and exchange it as a raw byte vector. The
//  byte buffer is the boundary: the scene hands those bytes to assets::write_file
//  / receives them from assets::load_file, so this module never touches the disk
//  or SDL and is fully unit-testable in memory.
//
//  Format (see book ch30):
//      FARM 1
//      SIZE <w> <h>
//      TILES
//      <h lines, each w chars: '.'=grass ':'=soil '~'=water '#'=path>
//      OBJECTS <n>
//      <n lines: <kindChar> <x> <y>>   (kindChar: T R H F W)
//      FARMER <gx> <gy>                 (-1 -1 if there is no farmer)
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "games/iso/farm.hpp"

namespace iso {

// Serialize the whole farm (terrain + objects + farmer) to text bytes.
std::vector<uint8_t> save_farm(const Farm& f);

// Parse bytes into `f`. On success `f` becomes the loaded farm and returns true.
// On any malformed input `f` is left UNCHANGED and returns false (transactional).
bool load_farm(Farm& f, const std::vector<uint8_t>& bytes);

} // namespace iso
