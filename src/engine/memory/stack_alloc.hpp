// =============================================================================
//  engine/memory/stack_alloc.hpp  —  LIFO stack allocator
// =============================================================================
//  Like an Arena, but it supports freeing individual allocations — as long as you
//  free them in REVERSE order (last allocated, first freed). Each block carries a
//  tiny header recording the head position before it, so free() can pop exactly
//  back. This is the natural fit for nested/temporary scopes (push args, recurse,
//  pop) and gives O(1) frees with zero fragmentation, at the cost of the LIFO rule.
//
//      [hdr][ A ][hdr][  B  ][hdr][ C ] head→        free() must release C, then B…
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/memory/memory.hpp"

namespace mem {

class StackAllocator {
public:
    using Marker = std::size_t;

    explicit StackAllocator(std::size_t bytes);
    StackAllocator(void* buffer, std::size_t bytes);
    ~StackAllocator();

    StackAllocator(const StackAllocator&)            = delete;
    StackAllocator& operator=(const StackAllocator&) = delete;
    StackAllocator(StackAllocator&& o) noexcept;
    StackAllocator& operator=(StackAllocator&& o) noexcept;

    void* allocate(std::size_t size, std::size_t align = kDefaultAlign);
    template <typename T>
    T* alloc(std::size_t n = 1) {
        return static_cast<T*>(allocate(n * sizeof(T), alignof(T)));
    }

    // Free the MOST RECENT allocation (asserts in debug if `ptr` isn't the top).
    void free(void* ptr);

    Marker mark() const { return head_; }
    void   free_to(Marker m);   // pop everything down to `m`
    void   reset() { free_to(0); }

    std::size_t used()     const { return head_; }
    std::size_t capacity() const { return size_; }
    std::size_t peak()     const { return peak_; }

private:
    // Stored immediately before each user block. `prev` = head_ before this alloc,
    // `end` = head_ after it (used to verify LIFO order on free).
    struct Header {
        std::size_t prev;
        std::size_t end;
    };

    std::uint8_t* base_ = nullptr;
    std::size_t   size_ = 0;
    std::size_t   head_ = 0;
    std::size_t   peak_ = 0;
    bool          owns_ = false;
};

} // namespace mem
