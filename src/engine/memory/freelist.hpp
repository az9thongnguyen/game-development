// =============================================================================
//  engine/memory/freelist.hpp  —  general variable-size allocator
// =============================================================================
//  A real (if small) heap. It keeps an address-sorted linked list of free spans.
//  allocate() does FIRST-FIT — walk the list, take the first span big enough,
//  split off the remainder. free() inserts the span back and COALESCES it with any
//  adjacent free neighbours, which is what keeps fragmentation in check. Each live
//  allocation carries a tiny header so free() knows the span's size and where the
//  block really started (alignment can push the user pointer past the block start).
//
//   free list (sorted):  head ─▶ [span @0x100,48] ─▶ [span @0x200,96] ─▶ null
//   free() merges  [0x100,48] + adjacent [0x130,…]  →  one larger span.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/memory/memory.hpp"

namespace mem {

class FreeListAllocator {
public:
    explicit FreeListAllocator(std::size_t bytes);
    FreeListAllocator(void* buffer, std::size_t bytes);
    ~FreeListAllocator();

    FreeListAllocator(const FreeListAllocator&)            = delete;
    FreeListAllocator& operator=(const FreeListAllocator&) = delete;
    FreeListAllocator(FreeListAllocator&& o) noexcept;
    FreeListAllocator& operator=(FreeListAllocator&& o) noexcept;

    void* allocate(std::size_t size, std::size_t align = kDefaultAlign);
    void  free(void* ptr);

    template <typename T>
    T* alloc(std::size_t n = 1) {
        return static_cast<T*>(allocate(n * sizeof(T), alignof(T)));
    }

    std::size_t capacity()  const { return size_; }
    std::size_t used()      const { return used_; }   // bytes handed out (incl. headers/padding)
    std::size_t peak()      const { return peak_; }
    std::size_t free_spans() const;                    // count of free-list nodes (fragmentation probe)

private:
    // A free span: header lives at the START of the span; `size` covers the whole span.
    struct FreeNode {
        std::size_t size;
        FreeNode*   next;
    };
    // Stored just before each live block. `size` = span consumed from the free list;
    // `adjust` = bytes from the span start to the user pointer (alignment padding).
    struct AllocHeader {
        std::size_t size;
        std::size_t adjust;
    };

    void init();

    void*         raw_       = nullptr;
    std::uint8_t* base_      = nullptr;
    std::size_t   size_      = 0;
    FreeNode*     free_head_ = nullptr;
    std::size_t   used_      = 0;
    std::size_t   peak_      = 0;
    bool          owns_      = false;
};

} // namespace mem
