// =============================================================================
//  engine/memory/arena.cpp  —  Arena implementation
// =============================================================================
#include "engine/memory/arena.hpp"

#include <cassert>
#include <cstdlib>

namespace mem {

Arena::Arena(std::size_t bytes)
    : base_(static_cast<std::uint8_t*>(std::malloc(bytes))), size_(bytes), owns_(true) {
    if (!base_) oom_fatal("Arena: backing malloc failed");   // release-safe (not just assert)
}

Arena::Arena(void* buffer, std::size_t bytes)
    : base_(static_cast<std::uint8_t*>(buffer)), size_(bytes), owns_(false) {
    assert(buffer && "Arena: borrowing ctor buffer must not be null");
}

Arena::~Arena() {
    if (owns_) std::free(base_);
}

Arena::Arena(Arena&& o) noexcept
    : base_(o.base_), size_(o.size_), head_(o.head_), peak_(o.peak_), owns_(o.owns_) {
    o.base_ = nullptr;
    o.size_ = o.head_ = o.peak_ = 0;
    o.owns_ = false;
}

Arena& Arena::operator=(Arena&& o) noexcept {
    if (this != &o) {
        if (owns_) std::free(base_);
        base_ = o.base_; size_ = o.size_; head_ = o.head_; peak_ = o.peak_; owns_ = o.owns_;
        o.base_ = nullptr; o.size_ = o.head_ = o.peak_ = 0; o.owns_ = false;
    }
    return *this;
}

void* Arena::allocate(std::size_t size, std::size_t align) {
    assert(is_pow2(align) && "Arena: align must be a power of two");
    if (size == 0) size = 1;   // a 0-byte alloc must still yield a distinct pointer
    // Align the ABSOLUTE address (base_ may only be malloc-aligned, e.g. 16), then
    // fold the alignment padding back into head_ so rewind/reset still work.
    void* const      p        = align_ptr(base_ + head_, align);
    const std::size_t new_head = static_cast<std::size_t>(static_cast<std::uint8_t*>(p) - base_) + size;
    if (new_head > size_) return nullptr;   // out of space is a normal, checkable result
    head_ = new_head;
    if (head_ > peak_) peak_ = head_;
    debug_fill(p, size, kFillAlloc);
    return p;
}

void Arena::rewind(Marker m) {
    assert(m <= head_ && "Arena: rewind marker is ahead of head");
    if (m < head_) debug_fill(base_ + m, head_ - m, kFillFree);
    head_ = m;
}

} // namespace mem
