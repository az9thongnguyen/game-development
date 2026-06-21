// =============================================================================
//  engine/memory/memory.hpp  —  shared foundation for the custom allocators
// =============================================================================
//  Five hand-written allocators (Arena, Stack, Pool, FreeList, Frame) build on
//  this tiny header: alignment math, a Stats block, the out-of-space convention,
//  and an opt-in debug fill. They are STORAGE allocators — they hand back raw,
//  correctly-aligned bytes and never construct or destroy objects. Placement-new
//  and destructor calls are the caller's job. All single-threaded (see the
//  subsystem-A spec); pure pointer math, so they run unchanged on the web.
// =============================================================================
#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace mem {

// Default alignment: 16 bytes covers SIMD-friendly types and matches what malloc
// hands out on most 64-bit targets.
inline constexpr std::size_t kDefaultAlign = 16;

inline constexpr bool is_pow2(std::size_t x) { return x != 0 && (x & (x - 1)) == 0; }

// Round an integer up to the next multiple of `align` (a power of two).
inline std::size_t align_up(std::size_t n, std::size_t align) {
    assert(is_pow2(align) && "align_up: align must be a non-zero power of two");
    return (n + (align - 1)) & ~(align - 1);
}

// A backing-allocation failure (malloc returned null) is fatal and unrecoverable:
// terminate loudly rather than return a half-built allocator that would invoke UB on
// first use. Unlike assert(), this still fires in NDEBUG/release builds.
[[noreturn]] inline void oom_fatal(const char* what) {
    std::fprintf(stderr, "mem: fatal — %s\n", what);
    std::abort();
}

// Round a pointer up to `align`.
inline void* align_ptr(void* p, std::size_t align) {
    const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(p);
    const std::uintptr_t m = static_cast<std::uintptr_t>(align) - 1;
    return reinterpret_cast<void*>((a + m) & ~m);
}

inline std::size_t bigger(std::size_t a, std::size_t b) { return a > b ? a : b; }

// Lightweight per-allocator accounting.
struct Stats {
    std::size_t capacity = 0;   // total bytes (or blocks) the allocator manages
    std::size_t used     = 0;   // currently handed out
    std::size_t peak     = 0;   // high-water mark of `used`
    std::size_t count    = 0;   // live allocations (where the allocator tracks it)
};

// Opt-in poison fill: 0xCD = freshly allocated (so uninitialized reads scream),
// 0xDD = just freed (so use-after-free scream). Compiled out unless MEM_DEBUG_FILL.
inline constexpr bool kDebugFill =
#ifdef MEM_DEBUG_FILL
    true;
#else
    false;
#endif

inline void debug_fill(void* p, std::size_t n, unsigned char byte) {
    if (kDebugFill && p && n) std::memset(p, byte, n);
}

// Poison bytes, named so call sites read clearly.
inline constexpr unsigned char kFillAlloc = 0xCD;
inline constexpr unsigned char kFillFree  = 0xDD;

} // namespace mem
