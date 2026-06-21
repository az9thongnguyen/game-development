// =============================================================================
//  engine/memory/arena.hpp  —  linear (bump) allocator
// =============================================================================
//  The simplest fast allocator: keep one "head" offset and bump it forward on
//  every allocation. There is no per-allocation free — you either rewind to a
//  saved marker or reset the whole thing. Perfect for "allocate a bunch of stuff
//  with the same lifetime, then throw it all away at once" (a level load, one
//  frame of scratch, a parse).
//
//      base_                head_                       base_+size_
//      ┌──────┬──────┬──────┬───────────────────────────────┐
//      │ A    │ B    │ C    │  free →                        │
//      └──────┴──────┴──────┴───────────────────────────────┘
//        allocate bumps head_ right; rewind/reset moves it left.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/memory/memory.hpp"

namespace mem {

class Arena {
public:
    using Marker = std::size_t;   // an offset into the buffer = a saved head_

    // Owning: allocate `bytes` from malloc and manage it.
    explicit Arena(std::size_t bytes);
    // Borrowing: bump within a buffer the caller owns (not freed by us).
    Arena(void* buffer, std::size_t bytes);
    ~Arena();

    Arena(const Arena&)            = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&& o) noexcept;
    Arena& operator=(Arena&& o) noexcept;

    // Return `size` bytes aligned to `align`, or nullptr if it would overflow.
    void* allocate(std::size_t size, std::size_t align = kDefaultAlign);

    // Typed convenience: raw storage for `n` T's (no construction).
    template <typename T>
    T* alloc(std::size_t n = 1) {
        return static_cast<T*>(allocate(n * sizeof(T), alignof(T)));
    }

    Marker mark() const { return head_; }   // remember the current head
    void   rewind(Marker m);                 // free everything allocated after `m`
    void   reset() { rewind(0); }            // free everything

    std::size_t used()     const { return head_; }
    std::size_t capacity() const { return size_; }
    std::size_t peak()     const { return peak_; }

    // RAII: rewind to the marker captured at construction when the scope exits.
    class Scope {
    public:
        explicit Scope(Arena& a) : arena_(a), marker_(a.mark()) {}
        ~Scope() { arena_.rewind(marker_); }
        Scope(const Scope&)            = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        Arena&  arena_;
        Marker  marker_;
    };

private:
    std::uint8_t* base_ = nullptr;
    std::size_t   size_ = 0;
    std::size_t   head_ = 0;
    std::size_t   peak_ = 0;
    bool          owns_ = false;
};

} // namespace mem
