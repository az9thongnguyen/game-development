// =============================================================================
//  games/iso/objkind.hpp  —  the kinds of object that sit on a tile
// =============================================================================
//  Shared by the ECS (a Renderable carries an ObjKind) and the Farm (placement,
//  blocking). Kept tiny and standalone so neither has to depend on the other.
// =============================================================================
#pragma once

#include <cstdint>

namespace iso {

// Things that can occupy a tile, on TOP of its terrain. One per tile in M4.
enum class ObjKind : uint8_t { Tree, Rock, House, Fence, Wheat };

// Whether an object blocks walking. Wheat (a crop) is passable; the structures
// and natural obstacles block. Used by the farm's walkability test (A* input).
inline bool blocks(ObjKind k) { return k != ObjKind::Wheat; }

} // namespace iso
