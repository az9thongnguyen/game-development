// =============================================================================
//  engine/memory/freelist.cpp  —  FreeListAllocator implementation
// =============================================================================
#include "engine/memory/freelist.hpp"

#include <cassert>
#include <cstdlib>

namespace mem {

FreeListAllocator::FreeListAllocator(std::size_t bytes) : owns_(true) {
    // Over-allocate by alignof(FreeNode) so that after aligning base_, base_+bytes is
    // still within the malloc'd region (alignment loss <= the extra bytes) — hence
    // size_ = bytes is exact and never OOB.
    raw_  = std::malloc(bytes + alignof(FreeNode));
    if (!raw_) oom_fatal("FreeListAllocator: backing malloc failed");
    base_ = static_cast<std::uint8_t*>(align_ptr(raw_, alignof(FreeNode)));
    size_ = bytes;
    init();
}

FreeListAllocator::FreeListAllocator(void* buffer, std::size_t bytes) : owns_(false) {
    assert(buffer && "FreeListAllocator: borrowing ctor buffer must not be null");
    base_ = static_cast<std::uint8_t*>(align_ptr(buffer, alignof(FreeNode)));
    const std::size_t lost = static_cast<std::size_t>(base_ - static_cast<std::uint8_t*>(buffer));
    size_ = (bytes > lost) ? bytes - lost : 0;
    init();
}

FreeListAllocator::~FreeListAllocator() {
    if (owns_) std::free(raw_);
}

FreeListAllocator::FreeListAllocator(FreeListAllocator&& o) noexcept
    : raw_(o.raw_), base_(o.base_), size_(o.size_), free_head_(o.free_head_),
      used_(o.used_), peak_(o.peak_), owns_(o.owns_) {
    o.raw_ = nullptr; o.base_ = nullptr; o.free_head_ = nullptr;
    o.size_ = o.used_ = o.peak_ = 0; o.owns_ = false;
}

FreeListAllocator& FreeListAllocator::operator=(FreeListAllocator&& o) noexcept {
    if (this != &o) {
        if (owns_) std::free(raw_);
        raw_ = o.raw_; base_ = o.base_; size_ = o.size_; free_head_ = o.free_head_;
        used_ = o.used_; peak_ = o.peak_; owns_ = o.owns_;
        o.raw_ = nullptr; o.base_ = nullptr; o.free_head_ = nullptr;
        o.size_ = o.used_ = o.peak_ = 0; o.owns_ = false;
    }
    return *this;
}

void FreeListAllocator::init() {
    if (size_ >= sizeof(FreeNode)) {
        free_head_       = reinterpret_cast<FreeNode*>(base_);   // one big free span
        free_head_->size = size_;
        free_head_->next = nullptr;
    } else {
        free_head_ = nullptr;
    }
    used_ = 0;
}

void* FreeListAllocator::allocate(std::size_t size, std::size_t align) {
    assert(is_pow2(align));
    if (size == 0) size = 1;
    const std::size_t a = bigger(align, alignof(AllocHeader));

    FreeNode* prev = nullptr;
    FreeNode* node = free_head_;
    while (node) {
        std::uint8_t* bp   = reinterpret_cast<std::uint8_t*>(node);
        const std::size_t user_off = align_up(reinterpret_cast<std::uintptr_t>(bp) + sizeof(AllocHeader), a)
                                     - reinterpret_cast<std::uintptr_t>(bp);
        // Span we must take from this node, rounded so any split point stays
        // FreeNode-aligned.
        const std::size_t consumed = align_up(user_off + size, alignof(FreeNode));

        if (consumed <= node->size) {
            FreeNode*         next      = node->next;
            const std::size_t remainder = node->size - consumed;
            std::size_t       taken;

            if (remainder >= sizeof(FreeNode)) {
                // Split: leave the tail as a new free node.
                FreeNode* split = reinterpret_cast<FreeNode*>(bp + consumed);
                split->size = remainder;
                split->next = next;
                (prev ? prev->next : free_head_) = split;
                taken = consumed;
            } else {
                // Too small to split — hand over the whole span.
                (prev ? prev->next : free_head_) = next;
                taken = node->size;
            }

            std::uint8_t* user = bp + user_off;
            AllocHeader*  h    = reinterpret_cast<AllocHeader*>(user - sizeof(AllocHeader));
            h->size   = taken;
            h->adjust = user_off;

            used_ += taken;
            if (used_ > peak_) peak_ = used_;
            debug_fill(user, size, kFillAlloc);
            return user;
        }
        prev = node;
        node = node->next;
    }
    return nullptr;   // no span big enough (out of space or too fragmented)
}

void FreeListAllocator::free(void* ptr) {
    if (!ptr) return;
    auto*             up   = static_cast<std::uint8_t*>(ptr);
    assert(up >= base_ + sizeof(AllocHeader) && up <= base_ + size_ &&
           "FreeListAllocator::free: pointer not from this allocator");
    const AllocHeader* h   = reinterpret_cast<AllocHeader*>(up - sizeof(AllocHeader));
    std::uint8_t*     bp   = up - h->adjust;
    const std::size_t span = h->size;

    assert(used_ >= span && "FreeListAllocator::free: used_ underflow (double-free?)");
    used_ -= span;
    debug_fill(bp, span, kFillFree);

    FreeNode* block = reinterpret_cast<FreeNode*>(bp);
    block->size = span;

    // Insert into the address-sorted free list.
    FreeNode* prev = nullptr;
    FreeNode* cur  = free_head_;
    while (cur && reinterpret_cast<std::uint8_t*>(cur) < bp) {
        prev = cur;
        cur  = cur->next;
    }
    block->next = cur;
    (prev ? prev->next : free_head_) = block;

    // Coalesce forward (block + next are contiguous).
    if (cur && bp + block->size == reinterpret_cast<std::uint8_t*>(cur)) {
        block->size += cur->size;
        block->next  = cur->next;
    }
    // Coalesce backward (prev + block are contiguous).
    if (prev && reinterpret_cast<std::uint8_t*>(prev) + prev->size == bp) {
        prev->size += block->size;
        prev->next  = block->next;
    }
}

std::size_t FreeListAllocator::free_spans() const {
    std::size_t n = 0;
    for (const FreeNode* p = free_head_; p; p = p->next) ++n;
    return n;
}

} // namespace mem
