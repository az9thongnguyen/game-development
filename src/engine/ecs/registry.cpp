// =============================================================================
//  engine/ecs/registry.cpp  —  entity allocator (non-template parts)
// =============================================================================
#include "engine/ecs/registry.hpp"

namespace ecs {

Entity Registry::create() {
    std::uint32_t index;
    if (!free_.empty()) {
        index = free_.back();           // recycle a freed slot…
        free_.pop_back();
        // destroy() only frees slots with a nonzero generation (exhausted slots are
        // retired, never freed), so a recycled slot is always >= 1.
        assert(generations_[index] >= 1 && "freed slot must have a nonzero generation");
    } else {
        index = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(1);      // …or grow a fresh one, starting at gen 1
    }
    ++alive_;
    return Entity{index, generations_[index]};
}

bool Registry::valid(Entity e) const {
    return e.generation != 0
        && e.index < generations_.size()
        && generations_[e.index] == e.generation;
}

void Registry::destroy(Entity e) {
    if (!valid(e)) return;
    for (auto& p : pools_) {                 // drop all of this entity's components
        if (p && p->has(e.index)) p->remove(e.index);
    }
    const std::uint32_t next = generations_[e.index] + 1;   // invalidate stale handles
    if (next == 0) {
        // Generation space for this slot is exhausted (2^32-1 recycles): RETIRE it
        // permanently — set gen 0 (always fails valid()) and never recycle — so a
        // wrapped generation can never resurrect an ancient handle (the ABA bug).
        generations_[e.index] = 0;
    } else {
        generations_[e.index] = next;
        free_.push_back(e.index);
    }
    --alive_;
}

} // namespace ecs
