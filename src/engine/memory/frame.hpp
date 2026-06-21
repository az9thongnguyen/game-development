// =============================================================================
//  engine/memory/frame.hpp  —  double-buffered per-frame scratch allocator
// =============================================================================
//  Per-frame "scratch" memory: things you allocate this frame and don't need next
//  frame (temporary arrays, transient render lists). A single arena reset each
//  frame would work — but then data can't survive into the next frame. The
//  double-buffered FrameAllocator keeps TWO arenas and flips between them, so data
//  allocated in frame N is still valid through frame N+1 (read last frame while
//  writing this frame), then reclaimed automatically.
//
//      frame N:   write → A      read ← B (frame N-1)
//      flip()  → swap
//      frame N+1: write → B      read ← A (frame N)      A reset for N+2 on next flip
// =============================================================================
#pragma once

#include <cstddef>

#include "engine/memory/arena.hpp"
#include "engine/memory/memory.hpp"

namespace mem {

class FrameAllocator {
public:
    // Each of the two buffers holds `bytes_per_buffer` bytes.
    explicit FrameAllocator(std::size_t bytes_per_buffer);

    // Holds pointers into its own members, so it is neither copyable nor movable
    // (it is meant to be a long-lived owned member, not passed around by value).
    FrameAllocator(const FrameAllocator&)            = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;
    FrameAllocator(FrameAllocator&&)                 = delete;
    FrameAllocator& operator=(FrameAllocator&&)      = delete;

    void* allocate(std::size_t size, std::size_t align = kDefaultAlign) {
        return cur_->allocate(size, align);
    }
    template <typename T>
    T* alloc(std::size_t n = 1) {
        return static_cast<T*>(allocate(n * sizeof(T), alignof(T)));
    }

    // Call once per frame: the current buffer becomes "last frame", and the other
    // buffer is reset and becomes current for the new frame.
    void flip();

    std::size_t used()     const { return cur_->used(); }
    std::size_t capacity() const { return cur_->capacity(); }

private:
    Arena  a_;
    Arena  b_;
    Arena* cur_;    // this frame's writes go here
    Arena* prev_;   // last frame's data, still readable
};

} // namespace mem
