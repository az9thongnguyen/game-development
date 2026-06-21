# Subsystem A — Memory Allocators — Design Spec

> Date: 2026-06-21 · First of the 6-subsystem program (A→F) · Branch `feat/mem-allocators`
> Program decomposition: **A allocators** → B ECS → C job system → D asset pipeline →
> E physics → F editor. This spec covers **A only**; each later subsystem gets its own.

## 1. Goal

Give the engine a set of **hand-written custom allocators** so later subsystems (ECS
component pools, physics bodies, asset buffers, per-frame scratch) can allocate fast,
contiguously, and without per-allocation `malloc`/`free` churn. Each allocator teaches
one classic memory-management idea, in the project's mentor style.

Today everything uses `std::vector`/`new` (fine, but opaque). This module makes
allocation *explicit and owned*, which is the foundation the other five subsystems
build on.

## 2. Scope (decided)

Five allocators, single-threaded, in `src/engine/memory/` under namespace `mem`:

| Allocator | Idea it teaches | Free model |
|-----------|-----------------|-----------|
| **Arena** (linear/bump) | pointer-bump allocation; mark/rewind | all-at-once (`reset`) or to a saved marker |
| **StackAllocator** | LIFO allocation with markers | pop back to a marker (strict LIFO) |
| **PoolAllocator** | fixed-size blocks + embedded free list | per-block `free` (O(1), any order) |
| **FreeListAllocator** | general variable-size, headers, coalescing | per-block `free` with adjacent-merge |
| **FrameAllocator** | double-buffered per-frame scratch | `flip()` each frame (this/next buffer) |

**Not in scope** (declined "comprehensive" option): TLSF/buddy general allocator,
guard pages, full leak tracking. We keep lightweight **stats** (below) instead.

**Threading:** none. Allocators are not thread-safe (the engine stays single-threaded;
the per-thread-ownership pattern means a future job system rarely needs a locked
allocator). The threading posture is revisited in subsystem **C**.

## 3. Common design (all five)

- **Backing memory:** each allocator either **owns** a buffer it `malloc`s once (ctor
  takes a byte size) **or borrows** a caller-provided span (ctor takes `void* + size`,
  non-owning). Owned memory is freed in the destructor (RAII → no leaks).
- **Alignment:** every `allocate(size, align = kDefaultAlign)` returns memory aligned
  to `align` (a power of two; default 16). A shared `align_up(ptr, a)` helper does the
  math; misalignment padding is accounted for.
- **Out of space:** return `nullptr` (and `assert` in debug builds). No exceptions —
  callers check the pointer. This keeps allocators usable in `-fno-exceptions` contexts
  later and matches the engine's "return-and-check" style (e.g. `assets::load_file`).
- **Stats:** each tracks `used()`, `capacity()`, `peak()` (high-water mark), and
  `count()` (live allocations where meaningful). A `Stats` struct + accessor; near-zero
  cost. Optional debug fill (`0xCD` on allocate, `0xDD` on free) behind
  `MEM_DEBUG_FILL` so use-after-free/uninitialized reads surface in testing.
- **Lifetime/ownership:** non-copyable (they own raw memory), **movable** (transfer the
  buffer + reset the source). Clear `reset()`/`free()` semantics per type.
- **Typed helpers:** `T* alloc<T>(n=1)` convenience that sizes+aligns for `T` and
  (for trivially-constructible use) returns raw storage; placement-`new` is the
  caller's job (documented). We do **not** call destructors — these are storage
  allocators, not containers.
- **Web:** pure pointer math over a `malloc`'d block → compiles and runs unchanged
  under Emscripten. No atomics, no OS calls beyond `malloc`/`free`.

## 4. Per-allocator detail

### Arena (`arena.hpp/.cpp`)
Bump a pointer through the buffer. `allocate(size, align)` aligns the head, advances,
returns the old head (or `nullptr` if it would overflow). `Marker mark()` captures the
current offset; `rewind(Marker)` restores it (frees everything allocated since).
`reset()` rewinds to empty. A small RAII `Arena::Scope` captures a marker in its ctor
and rewinds in its dtor — scoped temporary allocations.

### StackAllocator (`stack_alloc.hpp/.cpp`)
Like Arena but enforces **LIFO**: `allocate` stores a tiny header (previous offset);
`free(ptr)` must free the most recent allocation and pops back to its header (asserts
on out-of-order frees in debug). `Marker`/`free_to(Marker)` for batch pops. Teaches why
a stack discipline gives O(1) frees without fragmentation.

### PoolAllocator (`pool.hpp/.cpp`)
Carves the buffer into N fixed-size blocks (`block_size` ≥ `sizeof(void*)`, aligned).
Free blocks form an intrusive singly-linked **free list** (each free block stores the
next free block's pointer in its own memory). `allocate()` pops the list head;
`free(ptr)` pushes it back — both O(1), any order. Asserts on pointers outside the pool
or obviously double-freed (debug). Teaches the intrusive free-list trick.

### FreeListAllocator (`freelist.hpp/.cpp`)
General variable-size allocation. Maintains a linked list of free spans; `allocate`
does **first-fit** (split the chosen span, leave the remainder free); `free` inserts the
span back, sorted by address, and **coalesces** with adjacent free neighbours to fight
fragmentation. Each allocation carries a small header (size) so `free(ptr)` knows the
block size. Teaches splitting, headers, and coalescing — the heart of a real heap.

### FrameAllocator (`frame.hpp/.cpp`)
Two arenas, A and B. Allocations go to the **current** arena; `flip()` (call once per
frame) swaps current/previous and `reset`s the new current. So memory allocated in
frame N stays valid through frame N+1 (read last frame's data while writing this
frame's), then is reclaimed. Teaches double-buffering for transient per-frame data.

## 5. Files & build

```
src/engine/memory/
  memory.hpp        align helpers (align_up, is_pow2), Stats, kDefaultAlign, debug-fill
  arena.hpp/.cpp
  stack_alloc.hpp/.cpp
  pool.hpp/.cpp
  freelist.hpp/.cpp
  frame.hpp/.cpp
tests/test_mem.cpp  CTest 'mem'
```

CMake: a `mem_core` static lib (the five `.cpp`), `PUBLIC` include `src`, links
`engine_flags`. `test_mem` links `mem_core` + `engine_flags`, registered as ctest `mem`.
No SDL, no engine deps — pure, like the other core libs.

## 6. Testing (`tests/test_mem.cpp`)

Dependency-free CHECK-macro suite:
- **align_up / is_pow2** correctness; returned pointers honor requested alignment.
- **Arena:** sequential allocs don't overlap; `mark`/`rewind` frees correctly; `reset`;
  overflow → `nullptr`; `Scope` rewinds on scope exit; stats/peak.
- **Stack:** LIFO free pops correctly; `free_to(marker)`; stats.
- **Pool:** allocate up to N blocks then `nullptr`; free + re-allocate reuses a block;
  arbitrary free order; blocks are `block_size`-aligned and non-overlapping.
- **FreeList:** alloc/free patterns; **coalescing** (free two neighbours → one big span
  usable by a larger alloc); first-fit split leaves a usable remainder; fragmentation
  case.
- **Frame:** allocate in "frame N", `flip()`, previous-buffer pointer still valid while
  new allocations come from the other buffer; second `flip()` reclaims.
- **OOM** returns `nullptr` everywhere (no crash). 
- Run under **ASan+UBSan** (alignment UB, OOB) — must be clean.

## 7. Verification

- Build warning-clean (`-Wall -Wextra -Wpedantic`).
- `ctest` `mem` green; full suite stays green.
- ASan+UBSan run of `test_mem` clean; `leaks` shows 0 (RAII frees backing buffers).
- Offline micro-bench note (optional): arena vs `new` for many small allocs — to make
  the "why" concrete in the chapter (not a gate).

## 8. Integration policy

Provide the module standalone and **do not refactor existing systems** in this spec
(YAGNI; avoid unrelated churn). The chapter documents how B–F adopt it: ECS component
pools over a Pool/Arena, physics contacts in a FrameAllocator, asset loads into an
Arena, etc. A later subsystem may migrate a hot path as part of *its* spec.

## 9. Guidebook (split into several small, focused chapters)

Per the user's preference, the docs are **not one giant chapter** but four short,
easy-to-follow ones:

- **34 — Why custom allocators + the common foundation:** fragmentation/cache/churn
  motivation, alignment (`align_up`, power-of-two), ownership (own vs borrow), stats,
  the return-`nullptr`-on-OOM convention, "storage not containers" rule.
- **35 — The linear family: Arena & Stack:** bump allocation, mark/rewind, the
  `Scope` RAII, LIFO discipline. Concept → code → diagram → pitfalls → exercises.
- **36 — The free-list family: Pool & FreeList:** intrusive free list (Pool),
  variable-size first-fit + split + coalescing (FreeList).
- **37 — The FrameAllocator + adoption plan:** double-buffering for per-frame scratch,
  and how subsystems B–F will use each allocator.

Each chapter keeps the house shape (concept → code walkthrough → diagram → worked
example → pitfalls → glossary → exercises). Update `00-overview` reading order + README.

## 10. Risks / decisions

- **No destructor calls:** storage allocators, not containers — documented; callers
  placement-new / call dtors themselves. Prevents surprising double-destruction.
- **No thread safety:** intentional (see §2); the per-thread pattern is explained so C
  isn't boxed in.
- **first-fit (not best-fit) in FreeList:** simpler, good enough, teaches the concept;
  best-fit/segregated lists noted as exercises.
- **Owning vs borrowing ctors:** both supported so allocators can nest (a Pool carved
  out of an Arena's memory) — a natural advanced example.
