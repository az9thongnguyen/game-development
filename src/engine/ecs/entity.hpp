// =============================================================================
//  engine/ecs/entity.hpp  —  a safe entity handle (index + generation)
// =============================================================================
//  The farm ECS (ch28) used a plain incrementing id. The danger: destroy entity 7,
//  create a new one that reuses slot 7, and an old handle to "7" now silently points
//  at a DIFFERENT entity. The fix is a generation counter: each handle carries the
//  generation it was minted with; the registry bumps the slot's generation on
//  destroy, so a stale handle no longer matches and `valid()` returns false.
//
//      Entity{ index=7, generation=1 }   destroy → slot 7 generation becomes 2
//      create() reuses slot 7 →           Entity{ index=7, generation=2 }
//      the old {7,1} handle is now detectably stale.
// =============================================================================
#pragma once

#include <cstdint>

namespace ecs {

struct Entity {
    std::uint32_t index      = 0;
    std::uint32_t generation = 0;   // 0 is never handed out → a "null" handle
};

inline bool operator==(Entity a, Entity b) {
    return a.index == b.index && a.generation == b.generation;
}
inline bool operator!=(Entity a, Entity b) { return !(a == b); }

inline constexpr Entity null_entity{0, 0};

} // namespace ecs
