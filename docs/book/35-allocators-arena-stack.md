# Chapter 35 — The Linear Family: Arena & Stack

> **What this is.** The two simplest, fastest allocators — both just bump a pointer
> forward. The **Arena** frees everything at once (or rewinds to a marker); the
> **Stack** adds per-allocation frees, as long as you free in reverse (LIFO) order.
> Code: `src/engine/memory/arena.{hpp,cpp}`, `stack_alloc.{hpp,cpp}`. Read chapter
> 34 first for the shared foundation (alignment, ownership, stats).

---

## 1. Arena (linear / bump allocator)

### Concept

Keep one `head_` offset. Every allocation aligns the head, hands back the old head,
and bumps it forward. There is **no per-object free** — you either `rewind` to a
saved marker or `reset` the whole thing. That sounds limiting; it's actually the
perfect fit for the most common allocation pattern in a game: *"allocate a pile of
stuff with the same lifetime, then drop it all at once."* A level's data, one frame's
temporaries, the nodes of a parse tree.

```
 base_                 head_                         base_+capacity
 ┌──────┬───────┬──────┬──────────────────────────────────┐
 │  A   │   B   │  C   │  free  →                          │
 └──────┴───────┴──────┴──────────────────────────────────┘
   allocate() bumps head_ right; rewind(marker)/reset() move it left.
```

### Code walkthrough

Allocation is a handful of lines — note it aligns the **absolute address** (chapter
34 §3) and folds the padding back into `head_`:

```cpp
void* Arena::allocate(std::size_t size, std::size_t align) {
    assert(is_pow2(align));
    if (size == 0) size = 1;                       // distinct pointer even for 0 bytes
    void* const p = align_ptr(base_ + head_, align);
    const std::size_t new_head =
        (static_cast<std::uint8_t*>(p) - base_) + size;
    if (new_head > size_) return nullptr;          // out of space → checkable result
    head_ = new_head;
    if (head_ > peak_) peak_ = head_;
    return p;
}
```

Freeing is just moving `head_` back:

```cpp
Marker mark() const { return head_; }   // a marker is simply a saved offset
void   rewind(Marker m) { head_ = m; }  // frees everything allocated after m
void   reset()          { rewind(0); }  // frees everything
```

### Markers and the `Scope` RAII

A **marker** captures the current head; `rewind` returns to it, reclaiming everything
since. This gives cheap nested temporary regions. The `Scope` helper makes it
exception-safe and automatic:

```cpp
{
    mem::Arena::Scope s(scratch);      // remembers head_ here
    Thing* tmp = scratch.alloc<Thing>(1000);
    process(tmp);
}   // ← Scope's destructor rewinds: all 1000 Things reclaimed, no leak
```

### Worked example

A 1 KiB arena, default 16-byte alignment:

```
 allocate(100,16): head 0 → p=base+0,  head=100
 allocate(100,16): head 100 → align_up(base+100,16): if base 16-aligned, p=base+112
                   (12 bytes padding to reach the next multiple of 16), head=212
 mark() → 212;  allocate(200) → head=412;  rewind(212) → head back to 212
```

### Pitfalls

- **Dangling pointers after rewind/reset.** Everything past the marker is gone;
  pointers into it now alias future allocations. Don't keep them.
- **Mixed lifetimes.** If object X must outlive object Y but you allocated Y first,
  an arena can't free Y without freeing X. Use a different allocator, or two arenas.
- **One arena, one thread.** No locking (chapter 34 / subsystem C).

---

## 2. StackAllocator (LIFO)

### Concept

A Stack is an Arena that lets you free *individual* allocations — provided you free
them **last-in, first-out**. Each block stores a tiny header recording the head
position before it, so `free(ptr)` can pop exactly back. This matches naturally
nested scopes: push some scratch, recurse, pop it; push more, pop it.

```
 [hdr][   A   ][hdr][  B  ][hdr][ C ] head→
                                  ▲ free() must release C first, then B, then A
```

### Code walkthrough

Each allocation lays down a `Header { prev, end }` immediately before the user
pointer. `prev` is where to pop back to; `end` lets debug builds verify you're
freeing the top:

```cpp
struct Header { std::size_t prev; std::size_t end; };

void* StackAllocator::allocate(std::size_t size, std::size_t align) {
    const std::size_t a    = bigger(align, alignof(Header));   // header must be aligned too
    void* const       ua   = align_ptr(base_ + head_ + sizeof(Header), a);
    const std::size_t user = (std::uint8_t*)ua - base_;
    if (user + size > size_) return nullptr;
    Header* h = (Header*)(base_ + user - sizeof(Header));      // header sits right before user
    h->prev = head_;
    h->end  = user + size;
    head_   = user + size;
    return ua;
}

void StackAllocator::free(void* ptr) {
    const Header* h = (const Header*)((std::uint8_t*)ptr - sizeof(Header));
    assert(h->end == head_ && "frees must be in reverse (LIFO) order");
    head_ = h->prev;                                            // pop back
}
```

The `bigger(align, alignof(Header))` is the trick that keeps the header itself
aligned: because the user pointer is aligned to at least `alignof(Header)`, the
header at `user − sizeof(Header)` is aligned too.

### Markers

Like the arena, `mark()`/`free_to(marker)` pop a whole batch at once — handy to clear
everything a function pushed without freeing each block by hand.

### Pitfalls

- **Out-of-order free.** Freeing B before C corrupts the stack. The `h->end == head_`
  assert catches it in debug; in release it's a silent bug — respect LIFO.
- **Storing the returned pointer past its pop.** Same dangling rule as the arena.
- **Header overhead.** Every allocation costs `sizeof(Header)` (16 bytes) plus
  alignment padding — fine for a few big blocks, wasteful for many tiny ones (use a
  Pool, chapter 36).

## 3. Arena vs Stack — which when?

| Need | Use |
|------|-----|
| Allocate a batch, free it all together | **Arena** (`reset`/`rewind`) |
| Free individual blocks, but in reverse order | **Stack** (`free`/`free_to`) |
| Per-frame scratch that must survive one frame | **FrameAllocator** (chapter 37) |
| Many same-size objects, freed any order | **Pool** (chapter 36) |
| Arbitrary sizes, freed any order | **FreeList** (chapter 36) |

## 4. Glossary

- **Bump allocation** — allocate by advancing a single pointer/offset.
- **Marker** — a saved head position you can rewind to.
- **LIFO** — last-in, first-out; the order a Stack requires for frees.
- **Header** — per-allocation bookkeeping stored next to the block.

## 5. Exercises

1. **Two-arena lifetimes.** Keep "level" data in one arena and "frame" scratch in
   another; reset only the scratch each frame. Why is this cleaner than one arena?
2. **Catch the LIFO bug.** Allocate A then B from a Stack, free A first, and watch the
   debug assert fire. Then fix the order.
3. **Measure padding.** Allocate alternating `char` and `double` from an arena and log
   `used()`; account for every padding byte.
4. **`Scope` nesting.** Nest two `Arena::Scope`s and confirm the inner one reclaims
   before the outer.

*(Next: chapter 36 — the free-list family, Pool & FreeList.)*
