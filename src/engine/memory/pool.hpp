// =============================================================================
//  engine/memory/pool.hpp  —  fixed-size block (pool) allocator
// =============================================================================
//  When every allocation is the SAME size (e.g. one ECS component, one particle),
//  a pool is unbeatable: carve the buffer into N equal blocks and thread the free
//  ones onto an intrusive singly-linked list — the "next free" pointer lives
//  inside each free block's own memory, so the free list costs zero extra storage.
//  allocate() pops the head, free() pushes it back; both O(1), any order, no
//  fragmentation (all blocks are interchangeable).
//
//   free list:  head ─▶ [blk3] ─▶ [blk1] ─▶ [blk4] ─▶ null   (used blocks aren't linked)
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/memory/memory.hpp"

namespace mem {

class PoolAllocator {
public:
    // Owning: room for `block_count` blocks of `block_size` bytes each.
    PoolAllocator(std::size_t block_size, std::size_t block_count,
                  std::size_t align = kDefaultAlign);
    // Borrowing: carve as many blocks as fit in the caller's buffer.
    PoolAllocator(void* buffer, std::size_t buffer_size, std::size_t block_size,
                  std::size_t align = kDefaultAlign);
    ~PoolAllocator();

    PoolAllocator(const PoolAllocator&)            = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&& o) noexcept;
    PoolAllocator& operator=(PoolAllocator&& o) noexcept;

    void* allocate();        // one block, or nullptr if the pool is exhausted
    void  free(void* ptr);   // return a block to the free list
    void  reset();           // free ALL blocks at once (rebuild the free list)

    template <typename T>
    T* alloc() { return static_cast<T*>(allocate()); }

    std::size_t block_size() const { return block_; }
    std::size_t capacity()   const { return count_; }   // total blocks
    std::size_t used()       const { return used_; }    // blocks in use
    std::size_t peak()       const { return peak_; }

private:
    void build_free_list();

    void*         raw_       = nullptr;   // malloc result (owning) for free()
    std::uint8_t* base_      = nullptr;   // aligned start of block region
    std::size_t   block_     = 0;         // per-block stride (>= sizeof(void*), aligned)
    std::size_t   count_     = 0;
    void*         free_head_ = nullptr;   // head of the intrusive free list
    std::size_t   used_      = 0;
    std::size_t   peak_      = 0;
    bool          owns_      = false;
};

} // namespace mem
