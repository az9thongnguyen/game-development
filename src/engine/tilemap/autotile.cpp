// =============================================================================
//  engine/tilemap/autotile.cpp
// =============================================================================
#include "engine/tilemap/autotile.hpp"

#include <array>

namespace tilemap {
namespace {

// mask -> index, and the count, built once on first use. Building the table by
// ENUMERATION rather than typing out 47 constants means the "47" is a result, not a
// claim: if the canonical rule were wrong the count would change and the test would
// say so.
struct Table {
    std::array<int, 256> index{};
    int                  count = 0;

    Table() {
        std::array<int, 256> seen{};
        seen.fill(-1);
        for (int m = 0; m < 256; ++m) {
            const std::uint8_t c = autotile_canonical(static_cast<std::uint8_t>(m));
            if (seen[c] < 0) seen[c] = count++;
            index[static_cast<std::size_t>(m)] = seen[c];
        }
    }
};

const Table& table() {
    static const Table t;
    return t;
}

} // namespace

std::uint8_t autotile_canonical(std::uint8_t mask) {
    std::uint8_t out = mask;
    // A diagonal only reads as "connected" when you can actually walk to it along
    // the two cardinals beside it. Otherwise the corner is an outside corner and the
    // diagonal is invisible.
    if (!((mask & kN) && (mask & kE))) out = static_cast<std::uint8_t>(out & ~kNE);
    if (!((mask & kS) && (mask & kE))) out = static_cast<std::uint8_t>(out & ~kSE);
    if (!((mask & kS) && (mask & kW))) out = static_cast<std::uint8_t>(out & ~kSW);
    if (!((mask & kN) && (mask & kW))) out = static_cast<std::uint8_t>(out & ~kNW);
    return out;
}

int autotile_index(std::uint8_t mask) { return table().index[mask]; }
int autotile_count()                  { return table().count; }

int autotile_line_index(std::uint8_t mask) {
    // Deliberately a bit shuffle and not a lookup table. The numbering is the sheet
    // layout, so anything that hides it — a table, an enum of sixteen names — puts a
    // step between "tile 5" in the art file and "north|south" in the code, and that
    // step is where a piece ends up in the wrong slot.
    return ((mask & kN) ? 1 : 0) | ((mask & kE) ? 2 : 0) |
           ((mask & kS) ? 4 : 0) | ((mask & kW) ? 8 : 0);
}

} // namespace tilemap
