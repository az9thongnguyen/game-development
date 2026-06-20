// =============================================================================
//  games/iso/objkind.hpp  —  the kinds of object that sit on a tile
// =============================================================================
//  Shared by the ECS (a Renderable carries an ObjKind) and the Farm (placement,
//  blocking). Kept tiny and standalone so neither has to depend on the other.
// =============================================================================
#pragma once

#include <cstdint>

namespace iso {

// Things that get drawn on TOP of a tile's terrain. The first five are placeable
// with the brush (one per tile in M4); Farmer is the special mobile agent — it is
// never placed by the brush, it is drawn as a figure, and it never blocks.
enum class ObjKind : uint8_t { Tree, Rock, House, Fence, Wheat, Farmer };

// Whether an object blocks walking (the farm's A* walkability test consults this).
// Only solid structures / natural obstacles block; crops and the farmer do not.
inline bool blocks(ObjKind k) {
    return k == ObjKind::Tree || k == ObjKind::Rock ||
           k == ObjKind::House || k == ObjKind::Fence;
}

} // namespace iso
