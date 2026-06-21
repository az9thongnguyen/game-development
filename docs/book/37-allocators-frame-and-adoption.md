# Chapter 37 — The FrameAllocator + Adoption Plan

> **What this is.** The fifth allocator — a **double-buffered per-frame scratch**
> allocator — and the plan for how the *rest* of the engine (subsystems B–F) will
> actually use the five allocators. Code: `src/engine/memory/frame.{hpp,cpp}`. This
> closes subsystem A.

---

## 1. The per-frame scratch problem

Every frame a game produces piles of *transient* data: a list of visible objects to
draw, contact pairs from physics, temporary arrays for a sort. You want to allocate
them cheaply and throw them away next frame — that's exactly an Arena you `reset()`
each frame.

But there's a twist: sometimes frame N+1 needs to *read* what frame N produced (last
frame's transforms for interpolation, the previous frame's visibility for temporal
reuse). A single arena that you reset at the top of the frame has already thrown that
away. The fix is **double buffering**.

## 2. FrameAllocator (double-buffered)

### Concept

Keep **two** arenas, A and B. Allocations go to the *current* one. Once per frame you
`flip()`: the current arena becomes "last frame" (still readable), and the other
arena is reset and becomes current. So data from frame N survives through frame N+1,
then is reclaimed automatically two flips later.

```
 frame N:    write → A          read ← B  (frame N-1's data)
 flip()  ─── swap ───
 frame N+1:  write → B          read ← A  (frame N's data, still valid)
 flip()  ─── swap ───
 frame N+2:  write → A (reset)  read ← B  (frame N+1's data)
```

### Code walkthrough

It's two `Arena`s and a pointer swap:

```cpp
class FrameAllocator {
    Arena a_, b_;
    Arena* cur_;     // this frame's writes
    Arena* prev_;    // last frame's data, still readable
public:
    explicit FrameAllocator(std::size_t bytes_per_buffer)
        : a_(bytes_per_buffer), b_(bytes_per_buffer), cur_(&a_), prev_(&b_) {}
    void* allocate(std::size_t n, std::size_t align) { return cur_->allocate(n, align); }
    void  flip() {
        Arena* tmp = cur_; cur_ = prev_; prev_ = tmp;   // swap…
        cur_->reset();                                  // …and clear the new current
    }
};
```

### Why it's non-movable

`cur_` and `prev_` point at the object's own members `a_`/`b_`. If you *moved* a
`FrameAllocator`, those pointers would still point at the **old** object's arenas —
instant dangling. So move/copy are `= delete`d; a `FrameAllocator` is meant to be a
long-lived owned member (one per renderer, say), not passed around by value. This is a
good general lesson: **a type that stores pointers into itself must not be trivially
movable.**

### Worked example (from the test)

```cpp
mem::FrameAllocator fa(1024);
int* a = fa.alloc<int>(); *a = 111;     // frame N, buffer A
fa.flip();                              // → buffer B current, A = "last frame"
int* b = fa.alloc<int>(); *b = 222;     // frame N+1, buffer B
assert(*a == 111);                      // last frame's data STILL valid
fa.flip();                              // → buffer A current again, reset
int* c = fa.alloc<int>();
assert((void*)c == (void*)a);           // A was reset → same address reused
```

### Pitfalls

- **Holding data more than one frame.** It survives *one* flip, not two. For
  longer-lived data use an Arena or the FreeList.
- **Forgetting to `flip()`.** Then `cur_` fills up and never resets → out of space.
  Call `flip()` exactly once per frame, at a consistent point.
- **Sizing.** Each buffer is `bytes_per_buffer`; you pay for two. Size it from the
  observed `peak()` of a single frame.

## 3. Adoption plan — how B–F use the five allocators

Subsystem A is a **library**; per the spec we did **not** retrofit existing systems
(no unrelated churn). Here is how the upcoming subsystems will adopt it — each as part
of *its own* spec:

| Subsystem | Allocator(s) | Why |
|-----------|--------------|-----|
| **B — ECS** | **Pool** per component type; **Arena** for archetype/page storage | components are fixed-size and churn → pools; contiguous pages → arena (cache-friendly iteration) |
| **C — Job system** | **per-thread Arena/Frame** scratch | each worker bumps its own arena → no locks needed (the per-thread-ownership pattern from chapter 34) |
| **D — Asset pipeline** | **Arena** per load; **FreeList** for a cache | a load is one lifetime (arena, freed on unload); the cache holds mixed-size assets (freelist) |
| **E — Physics** | **FrameAllocator** for contacts/manifolds; **Pool** for bodies | contacts are per-frame transient (frame alloc); bodies are fixed-size, added/removed (pool) |
| **F — Editor** | **Arena** for transient UI/command buffers; **FreeList** general | per-frame immediate-mode UI scratch (arena); undo/redo command storage (freelist) |

The point of building A first is exactly this: every later subsystem gets fast,
explicit, cache-friendly memory without each reinventing it — and the `peak()` stats
make budgets measurable.

## 4. Subsystem A — done

Five allocators, all hand-written, all tested (unit tests + ASan/UBSan), all
warning-clean, none touching SDL — the engine-core memory foundation is in place.

```
mem::Arena · mem::StackAllocator · mem::PoolAllocator · mem::FreeListAllocator · mem::FrameAllocator
```

Next in the program: **B — the engine-core ECS** (generalizing the farm-specific
sparse set from chapter 28 into a reusable entity-component system, built on these
allocators).

## 5. Glossary

- **Double buffering** — keep two buffers, write one while reading the other, swap
  each frame.
- **Per-frame scratch** — memory whose lifetime is a single frame (or two, here).
- **Self-referential type** — one storing pointers into its own members; must not be
  moved (hence `= delete`).

## 6. Exercises

1. **Triple buffering.** Extend the FrameAllocator to keep data for *two* past frames.
   What changes in `flip()`?
2. **Frame stats.** Log the per-frame `peak()` and size each buffer to fit.
3. **Adopt it for real.** Pick the FPS raycaster's per-frame work and route one
   temporary array through a `FrameAllocator`; confirm no behavior change and no leak.
4. **Make it movable (carefully).** Could you store indices instead of pointers
   (`cur_` as a `bool`/`int`) so the FrameAllocator *can* be moved? Try it.
