// =============================================================================
//  tests/test_mem.cpp  —  the five custom allocators (no SDL)
// =============================================================================
#include "engine/memory/arena.hpp"
#include "engine/memory/frame.hpp"
#include "engine/memory/freelist.hpp"
#include "engine/memory/memory.hpp"
#include "engine/memory/pool.hpp"
#include "engine/memory/stack_alloc.hpp"

#include <cstdint>
#include <cstdio>

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool aligned(const void* p, std::size_t a) {
    return (reinterpret_cast<std::uintptr_t>(p) % a) == 0;
}

static void test_helpers() {
    CHECK(mem::is_pow2(16) && mem::is_pow2(1) && !mem::is_pow2(0) && !mem::is_pow2(24));
    CHECK(mem::align_up(0, 16) == 0);
    CHECK(mem::align_up(1, 16) == 16);
    CHECK(mem::align_up(16, 16) == 16);
    CHECK(mem::align_up(17, 8) == 24);
}

static void test_arena() {
    mem::Arena a(1024);
    CHECK(a.capacity() == 1024 && a.used() == 0);

    void* p1 = a.allocate(100, 16);
    void* p2 = a.allocate(100, 16);
    CHECK(p1 && p2 && p1 != p2);
    CHECK(aligned(p1, 16) && aligned(p2, 16));
    CHECK(static_cast<std::uint8_t*>(p2) >= static_cast<std::uint8_t*>(p1) + 100);  // no overlap

    // Strong alignment must hold even though malloc only guarantees 16.
    void* p64 = a.allocate(8, 64);
    CHECK(p64 && aligned(p64, 64));

    // mark / rewind.
    const mem::Arena::Marker m = a.mark();
    const std::size_t        before = a.used();
    a.allocate(200, 16);
    CHECK(a.used() > before);
    a.rewind(m);
    CHECK(a.used() == before);

    // overflow → nullptr (and does not corrupt state).
    CHECK(a.allocate(100000, 16) == nullptr);
    CHECK(a.used() == before);

    // Scope rewinds automatically.
    {
        mem::Arena::Scope s(a);
        a.allocate(300, 16);
        CHECK(a.used() > before);
    }
    CHECK(a.used() == before);

    a.reset();
    CHECK(a.used() == 0 && a.peak() >= before);
}

static void test_stack() {
    mem::StackAllocator s(1024);
    void* a = s.allocate(64, 16);
    void* b = s.allocate(64, 32);
    CHECK(a && b && aligned(a, 16) && aligned(b, 32));
    const std::size_t after_two = s.used();

    s.free(b);                       // LIFO: free the most recent first
    CHECK(s.used() < after_two);
    void* b2 = s.allocate(64, 16);   // space reused
    CHECK(b2 != nullptr);
    s.free(b2);
    s.free(a);
    CHECK(s.used() == 0);

    // free_to marker.
    const mem::StackAllocator::Marker m = s.mark();
    s.allocate(128, 16);
    s.allocate(128, 16);
    s.free_to(m);
    CHECK(s.used() == 0);

    CHECK(s.allocate(100000, 16) == nullptr);   // overflow
}

static void test_pool() {
    mem::PoolAllocator pool(32, 4);
    CHECK(pool.capacity() == 4 && pool.used() == 0);
    CHECK(pool.block_size() >= 32);

    void* b[4];
    for (int i = 0; i < 4; ++i) {
        b[i] = pool.allocate();
        CHECK(b[i] != nullptr && aligned(b[i], 16));
    }
    CHECK(pool.used() == 4);
    CHECK(pool.allocate() == nullptr);   // exhausted

    pool.free(b[1]);                      // free a middle block
    CHECK(pool.used() == 3);
    void* reused = pool.allocate();       // must reuse a freed block
    CHECK(reused == b[1]);
    CHECK(pool.used() == 4);

    for (int i = 0; i < 4; ++i) pool.free(i == 1 ? reused : b[i]);
    CHECK(pool.used() == 0);
}

static void test_freelist() {
    mem::FreeListAllocator fl(2048);
    CHECK(fl.free_spans() == 1 && fl.used() == 0);

    void* p1 = fl.allocate(64, 16);
    void* p2 = fl.allocate(64, 16);
    void* p3 = fl.allocate(64, 32);
    CHECK(p1 && p2 && p3 && p1 != p2 && p2 != p3);
    CHECK(aligned(p1, 16) && aligned(p3, 32));
    CHECK(fl.used() > 0);

    // Free in an order that exercises coalescing: middle, then both neighbours.
    fl.free(p2);
    fl.free(p1);
    fl.free(p3);
    CHECK(fl.used() == 0);
    CHECK(fl.free_spans() == 1);          // everything merged back into one span

    // The whole region is usable again after coalescing.
    void* big = fl.allocate(1500, 16);
    CHECK(big != nullptr && aligned(big, 16));
    fl.free(big);
    CHECK(fl.free_spans() == 1);

    // Out of space → nullptr.
    CHECK(fl.allocate(100000, 16) == nullptr);
}

static void test_frame() {
    mem::FrameAllocator fa(1024);
    int* a = fa.alloc<int>();
    *a = 111;
    void* addr_a = a;

    fa.flip();                            // frame N+1: cur = other buffer
    int* b = fa.alloc<int>();
    *b = 222;
    CHECK(*a == 111);                     // last frame's data still valid
    CHECK(static_cast<void*>(b) != static_cast<void*>(a));   // different buffer

    fa.flip();                            // frame N+2: back to the first buffer, reset
    int* c = fa.alloc<int>();
    CHECK(static_cast<void*>(c) == addr_a);   // first buffer was reset and reused
}

// Borrowing constructors + composition (a Pool carved out of an Arena) + Pool::reset.
static void test_borrowing_and_nesting() {
    // Arena over a caller-owned stack buffer (no malloc).
    alignas(16) static unsigned char raw[512];
    mem::Arena ba(raw, sizeof(raw));
    void* y = ba.allocate(32, 16);
    CHECK(y && aligned(y, 16));
    CHECK(static_cast<unsigned char*>(y) >= raw &&
          static_cast<unsigned char*>(y) < raw + sizeof(raw));   // lives in our buffer

    // A Pool nested inside an Arena's memory (the borrowing pool ctor).
    mem::Arena backing(4096);
    void*      region = backing.allocate(2048, 64);
    mem::PoolAllocator nested(region, 2048, 64);
    CHECK(nested.capacity() > 0);
    void* a = nested.allocate();
    void* b = nested.allocate();
    CHECK(a && b && a != b && aligned(a, 64) && aligned(b, 64));

    // Pool::reset frees everything at once.
    mem::PoolAllocator p(16, 3);
    p.allocate();
    p.allocate();
    CHECK(p.used() == 2);
    p.reset();
    CHECK(p.used() == 0);
    CHECK(p.allocate() != nullptr);
}

int main() {
    test_helpers();
    test_arena();
    test_stack();
    test_pool();
    test_freelist();
    test_frame();
    test_borrowing_and_nesting();
    if (g_failures == 0) std::printf("mem: all tests passed\n");
    else                 std::printf("mem: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
