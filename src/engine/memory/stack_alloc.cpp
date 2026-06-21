// =============================================================================
//  engine/memory/stack_alloc.cpp  —  StackAllocator implementation
// =============================================================================
#include "engine/memory/stack_alloc.hpp"

#include <cassert>
#include <cstdlib>

namespace mem {

StackAllocator::StackAllocator(std::size_t bytes)
    : base_(static_cast<std::uint8_t*>(std::malloc(bytes))), size_(bytes), owns_(true) {
    if (!base_) oom_fatal("StackAllocator: backing malloc failed");
}

StackAllocator::StackAllocator(void* buffer, std::size_t bytes)
    : base_(static_cast<std::uint8_t*>(buffer)), size_(bytes), owns_(false) {
    assert(buffer && "StackAllocator: borrowing ctor buffer must not be null");
}

StackAllocator::~StackAllocator() {
    if (owns_) std::free(base_);
}

StackAllocator::StackAllocator(StackAllocator&& o) noexcept
    : base_(o.base_), size_(o.size_), head_(o.head_), peak_(o.peak_), owns_(o.owns_) {
    o.base_ = nullptr; o.size_ = o.head_ = o.peak_ = 0; o.owns_ = false;
}

StackAllocator& StackAllocator::operator=(StackAllocator&& o) noexcept {
    if (this != &o) {
        if (owns_) std::free(base_);
        base_ = o.base_; size_ = o.size_; head_ = o.head_; peak_ = o.peak_; owns_ = o.owns_;
        o.base_ = nullptr; o.size_ = o.head_ = o.peak_ = 0; o.owns_ = false;
    }
    return *this;
}

void* StackAllocator::allocate(std::size_t size, std::size_t align) {
    assert(is_pow2(align) && "StackAllocator: align must be a power of two");
    if (size == 0) size = 1;
    // The header sits right before the user pointer, so the user pointer must be
    // aligned to at least alignof(Header). Align the ABSOLUTE address (base_ may
    // only be malloc-aligned), then derive the offset for bookkeeping.
    const std::size_t a       = bigger(align, alignof(Header));
    void* const       ua      = align_ptr(base_ + head_ + sizeof(Header), a);
    const std::size_t user    = static_cast<std::size_t>(static_cast<std::uint8_t*>(ua) - base_);
    if (user + size > size_) return nullptr;   // out of space: a normal, checkable result
    Header* h = reinterpret_cast<Header*>(base_ + user - sizeof(Header));
    h->prev = head_;
    h->end  = user + size;

    head_ = user + size;
    if (head_ > peak_) peak_ = head_;

    debug_fill(ua, size, kFillAlloc);
    return ua;
}

void StackAllocator::free(void* ptr) {
    if (!ptr) return;
    const Header* h = reinterpret_cast<const Header*>(static_cast<std::uint8_t*>(ptr) - sizeof(Header));
    assert(h->end == head_ && "StackAllocator: frees must be in reverse (LIFO) order");
    if (h->prev < head_) debug_fill(base_ + h->prev, head_ - h->prev, kFillFree);
    head_ = h->prev;
}

void StackAllocator::free_to(Marker m) {
    assert(m <= head_ && "StackAllocator: marker is ahead of head");
    if (m < head_) debug_fill(base_ + m, head_ - m, kFillFree);
    head_ = m;
}

} // namespace mem
