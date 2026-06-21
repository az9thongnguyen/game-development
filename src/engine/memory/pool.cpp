// =============================================================================
//  engine/memory/pool.cpp  —  PoolAllocator implementation
// =============================================================================
#include "engine/memory/pool.hpp"

#include <cassert>
#include <cstdlib>

namespace mem {

PoolAllocator::PoolAllocator(std::size_t block_size, std::size_t block_count, std::size_t align)
    : owns_(true) {
    assert(is_pow2(align));
    // Blocks must be aligned for BOTH the user type and the intrusive free-list
    // pointer stored inside a free block — hence at least alignof(void*).
    const std::size_t a = bigger(align, alignof(void*));
    block_ = align_up(bigger(block_size, sizeof(void*)), a);
    count_ = block_count;
    // Over-allocate by `a` so we can align the block region even though malloc
    // only guarantees max_align_t alignment.
    raw_   = std::malloc(block_ * count_ + a);
    if (!raw_) oom_fatal("PoolAllocator: backing malloc failed");
    base_  = static_cast<std::uint8_t*>(align_ptr(raw_, a));
    build_free_list();
}

PoolAllocator::PoolAllocator(void* buffer, std::size_t buffer_size, std::size_t block_size,
                             std::size_t align)
    : owns_(false) {
    assert(is_pow2(align));
    assert(buffer && "PoolAllocator: borrowing ctor buffer must not be null");
    const std::size_t a = bigger(align, alignof(void*));
    block_ = align_up(bigger(block_size, sizeof(void*)), a);
    base_  = static_cast<std::uint8_t*>(align_ptr(buffer, a));
    const std::size_t lost = static_cast<std::size_t>(base_ - static_cast<std::uint8_t*>(buffer));
    count_ = (buffer_size > lost) ? (buffer_size - lost) / block_ : 0;
    build_free_list();
}

PoolAllocator::~PoolAllocator() {
    if (owns_) std::free(raw_);
}

PoolAllocator::PoolAllocator(PoolAllocator&& o) noexcept
    : raw_(o.raw_), base_(o.base_), block_(o.block_), count_(o.count_),
      free_head_(o.free_head_), used_(o.used_), peak_(o.peak_), owns_(o.owns_) {
    o.raw_ = nullptr; o.base_ = nullptr; o.free_head_ = nullptr;
    o.block_ = o.count_ = o.used_ = o.peak_ = 0; o.owns_ = false;
}

PoolAllocator& PoolAllocator::operator=(PoolAllocator&& o) noexcept {
    if (this != &o) {
        if (owns_) std::free(raw_);
        raw_ = o.raw_; base_ = o.base_; block_ = o.block_; count_ = o.count_;
        free_head_ = o.free_head_; used_ = o.used_; peak_ = o.peak_; owns_ = o.owns_;
        o.raw_ = nullptr; o.base_ = nullptr; o.free_head_ = nullptr;
        o.block_ = o.count_ = o.used_ = o.peak_ = 0; o.owns_ = false;
    }
    return *this;
}

void PoolAllocator::reset() { build_free_list(); }   // all blocks free again

void PoolAllocator::build_free_list() {
    free_head_ = nullptr;
    // Link blocks back-to-front so the list head ends up at block 0 (tidy for tests).
    for (std::size_t i = count_; i-- > 0;) {
        void* block = base_ + i * block_;
        *static_cast<void**>(block) = free_head_;   // store "next free" inside the block
        free_head_ = block;
    }
    used_ = 0;
}

void* PoolAllocator::allocate() {
    if (!free_head_) return nullptr;   // pool exhausted: a normal, checkable result
    void* p   = free_head_;
    free_head_ = *static_cast<void**>(p);   // pop: head = head->next
    ++used_;
    if (used_ > peak_) peak_ = used_;
    debug_fill(p, block_, kFillAlloc);
    return p;
}

void PoolAllocator::free(void* ptr) {
    if (!ptr) return;
    auto* p = static_cast<std::uint8_t*>(ptr);
    assert(p >= base_ && p < base_ + count_ * block_ && "PoolAllocator: pointer not from this pool");
    assert(static_cast<std::size_t>(p - base_) % block_ == 0 && "PoolAllocator: misaligned free");
    assert(used_ > 0 && "PoolAllocator: free underflow (double-free?)");
    debug_fill(ptr, block_, kFillFree);
    *static_cast<void**>(ptr) = free_head_;  // push back onto the free list
    free_head_ = ptr;
    --used_;
}

} // namespace mem
