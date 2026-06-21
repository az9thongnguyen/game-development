# Chapter 36 — The Free-List Family: Pool & FreeList

> **What this is.** Two allocators that support **freeing in any order** — the thing
> the linear family (chapter 35) can't do. The **Pool** handles many objects of one
> fixed size with an *intrusive free list*; the **FreeList** handles arbitrary sizes
> with splitting and coalescing — a real, if small, heap. Code:
> `src/engine/memory/pool.{hpp,cpp}`, `freelist.{hpp,cpp}`.

---

## 1. PoolAllocator (fixed-size blocks)

### Concept

When every allocation is the **same size** — one ECS component, one particle, one
network packet — a pool is unbeatable. Carve the buffer into N equal blocks. Thread
the *free* blocks onto a singly-linked list whose "next" pointer is stored **inside
each free block's own memory** (it's free — we can use it!). That's an **intrusive**
free list: zero extra storage for bookkeeping.

```
 blocks:   [ used ][ FREE ][ used ][ FREE ][ FREE ]
                     │next           │next    │next
 free_head ──────────┘   ┌───────────┘        └──▶ null
                         └────────────────────────▶ (order is just however frees happened)
```

`allocate()` pops the list head; `free()` pushes the block back. Both **O(1)**, any
order, and **zero fragmentation** because all blocks are interchangeable.

### Code walkthrough

Building the free list threads every block to the next:

```cpp
void PoolAllocator::build_free_list() {
    free_head_ = nullptr;
    for (std::size_t i = count_; i-- > 0;) {        // back-to-front so head = block 0
        void* block = base_ + i * block_;
        *static_cast<void**>(block) = free_head_;    // store "next free" inside the block
        free_head_ = block;
    }
    used_ = 0;
}
```

Allocate and free are a pop and a push:

```cpp
void* PoolAllocator::allocate() {
    if (!free_head_) return nullptr;                 // exhausted
    void* p    = free_head_;
    free_head_ = *static_cast<void**>(p);            // head = head->next
    ++used_;
    return p;
}
void PoolAllocator::free(void* ptr) {
    assert(ptr >= base_ && ptr < base_ + count_*block_);   // from this pool?
    assert((ptr - base_) % block_ == 0);                   // on a block boundary?
    *static_cast<void**>(ptr) = free_head_;          // push back
    free_head_ = ptr;
    --used_;
}
```

Two details from chapter 34/subsystem A: blocks are sized/aligned to at least
`alignof(void*)` (so the intrusive pointer is well-aligned), and `block_size` is
bumped to ≥ `sizeof(void*)` (so a free block can hold the link). `reset()` rebuilds
the whole free list to reclaim everything at once.

### Pitfalls

- **Double-free** corrupts the list (a block ends up linked twice → handed out
  twice). We assert on `used_` underflow as a cheap symptom; a full guard (a
  used-bitmap) is an exercise.
- **Freeing a foreign or mid-block pointer.** The range + boundary asserts catch it.
- **One size only.** That's the deal — variable sizes go to the FreeList.

---

## 2. FreeListAllocator (variable sizes)

### Concept

A general allocator: any size, freed in any order. It keeps an **address-sorted
linked list of free spans**. `allocate` walks the list and takes the first span big
enough (**first-fit**), splitting off the leftover as a smaller free span. `free`
inserts the span back and **coalesces** it with adjacent free neighbours so the heap
doesn't shatter into unusable slivers. This is, in miniature, how a real `malloc`
works.

```
 free list (sorted by address):
   head ─▶ [span @0x100, 48B] ─▶ [span @0x200, 96B] ─▶ null

 allocate(32): first-fit takes @0x100, splits → [used 32] + [free @0x120, 16B]
 free(@0x120) next to [@0x100]: coalesce → one larger free span again
```

### Headers: finding the block on free

`free(ptr)` only gets a pointer — it must recover the block start and size. So each
live allocation carries a header just before the user pointer:

```cpp
struct AllocHeader { std::size_t size; std::size_t adjust; };
//   size   = total span taken from the free list
//   adjust = bytes from the span start to the user pointer (alignment padding)
```

### allocate: first-fit + split

```cpp
const std::size_t a = bigger(align, alignof(AllocHeader));
for (FreeNode* node = free_head_, *prev = nullptr; node; prev = node, node = node->next) {
    std::uint8_t* bp = (std::uint8_t*)node;
    std::size_t user_off = align_up((uintptr_t)bp + sizeof(AllocHeader), a) - (uintptr_t)bp;
    std::size_t consumed = align_up(user_off + size, alignof(FreeNode));  // keep splits aligned
    if (consumed <= node->size) {
        // split off the remainder if it's big enough to hold a FreeNode, else take it all
        ... unlink/relink ...
        AllocHeader* h = (AllocHeader*)(bp + user_off - sizeof(AllocHeader));
        h->size = taken; h->adjust = user_off;
        return bp + user_off;
    }
}
return nullptr;   // nothing big enough (out of space / too fragmented)
```

Rounding `consumed` up to `alignof(FreeNode)` guarantees the split point is aligned
for the next `FreeNode` written there — another subsystem-A correctness detail.

### free: insert sorted + coalesce both sides

```cpp
AllocHeader* h = (AllocHeader*)((uint8_t*)ptr - sizeof(AllocHeader));
uint8_t* bp = (uint8_t*)ptr - h->adjust;          // recover the span start
FreeNode* block = (FreeNode*)bp; block->size = h->size;
// …insert into the address-sorted list (find prev/cur)…
if (cur  && bp + block->size == (uint8_t*)cur)  { block->size += cur->size;  block->next = cur->next; }  // merge forward
if (prev && (uint8_t*)prev + prev->size == bp)  { prev->size  += block->size; prev->next  = block->next; } // merge backward
```

Coalescing is what keeps fragmentation in check: free three adjacent blocks in any
order and the free list collapses back to one big span (the test verifies
`free_spans() == 1` and that the whole region is allocatable again).

### Pitfalls

- **First-fit fragmentation.** First-fit is simple but can leave small gaps; best-fit
  or segregated free lists reduce that (exercises). Coalescing mitigates it.
- **Header corruption / double-free.** Writing before `ptr` assumes a valid header;
  freeing a foreign pointer reads garbage. We assert the pointer is in range and that
  `used_` won't underflow.
- **Per-allocation overhead.** Each block costs an `AllocHeader` plus alignment
  padding — for many tiny same-size objects, a Pool is far leaner.

## 3. Pool vs FreeList

| | Pool | FreeList |
|--|------|----------|
| Sizes | one fixed size | any size |
| Free order | any | any |
| Speed | O(1) alloc/free | O(n) first-fit walk |
| Fragmentation | none | possible (coalescing helps) |
| Overhead | ~0 (intrusive) | header + padding per block |

Rule of thumb: **same-size → Pool; mixed-size → FreeList.**

## 4. Glossary

- **Intrusive free list** — the "next free" link stored inside the free block itself.
- **First-fit** — take the first free span large enough.
- **Split** — carve a request out of a larger free span, leaving a smaller one.
- **Coalesce** — merge adjacent free spans back into one, fighting fragmentation.

## 5. Exercises

1. **Double-free guard.** Add a used-bitmap to the Pool and assert on double-free.
2. **Best-fit.** Change FreeList to scan the whole list and take the *smallest*
   sufficient span. Measure fragmentation vs first-fit on a churny workload.
3. **Segregated pools.** Build a small allocator that routes by size to several Pools
   (e.g. 16/32/64/128-byte) and falls back to the FreeList for big requests.
4. **Stress test.** Randomly alloc/free thousands of mixed sizes; assert the free
   list stays address-sorted and `used()` returns to 0 at the end.

*(Next: chapter 37 — the FrameAllocator and how subsystems B–F adopt all five.)*
