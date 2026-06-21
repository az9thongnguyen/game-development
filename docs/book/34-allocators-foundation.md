# Chapter 34 — Why Custom Allocators (and the Common Foundation)

> **What this is.** The opening of the post-M5 program. Subsystem **A** gives the
> engine five hand-written **memory allocators**. This first chapter explains *why*
> a game engine bothers (when `new`/`malloc` exist), and builds the shared
> foundation every allocator uses: **alignment**, **ownership**, **stats**, the
> out-of-space convention, and the "storage, not containers" rule. The five
> allocators themselves come in chapters 35–37. Code: `src/engine/memory/memory.hpp`.

---

## 1. Why not just use `new` / `malloc`?

`malloc` is a *general* allocator: any size, any order, thread-safe, fast enough.
That generality costs you four things a game cares about:

1. **Fragmentation.** Thousands of mixed-size alloc/free calls leave the heap full
   of unusable holes. Over a long play session this slowly wastes memory and slows
   allocation.
2. **Cache misses.** `malloc` scatters objects across the heap. Iterating them
   (every frame!) jumps all over memory; the CPU cache can't help.
3. **Per-call overhead.** Each `malloc`/`free` does bookkeeping, maybe takes a lock.
   Multiply by 100k particles per frame and it adds up.
4. **No control over lifetime.** You can't say "free *everything* I allocated for
   this level in one instruction."

A **custom allocator** trades generality for control. If you know the *pattern* of
your allocations — all the same size (a pool), all the same lifetime (an arena), all
this frame (a frame allocator) — you can beat `malloc` by a wide margin and lay
objects out contiguously for the cache. That's the whole game.

```
malloc heap (general):   [obj][hole][obj][obj][hole][obj]   ← scattered, fragmented
arena (same lifetime):   [obj][obj][obj][obj]→ free          ← packed, freed all at once
pool  (same size):       [▮][▮][ ][▮][ ][▮]                  ← fixed slots, O(1) reuse
```

## 2. Storage, not containers

A critical design rule for all five: they hand back **raw, aligned bytes**. They do
**not** construct or destroy your objects. If you want a `Foo` in arena memory:

```cpp
void* mem = arena.allocate(sizeof(Foo), alignof(Foo));
Foo*  f   = new (mem) Foo(args...);   // placement-new: construct in our bytes
// …use f…
f->~Foo();                            // YOU call the destructor (the arena won't)
```

Why not call constructors for you? Because mixing storage management with object
lifetime is where double-destruction and surprise bugs live. Keeping allocators as
pure *byte* providers makes them simple, predictable, and reusable for anything
(PODs, buffers, component arrays). The typed helper `alloc<T>(n)` just does the
`sizeof`/`alignof` math and returns `T*` storage — still no construction.

## 3. Alignment (the part everyone gets wrong)

A type `T` must live at an address that is a multiple of `alignof(T)` — reading a
`double` from a misaligned address is undefined behavior (and a crash on some CPUs).
So every `allocate(size, align)` must return a pointer that is a multiple of `align`.

`align` is always a **power of two**, which makes the math a couple of bit ops:

```cpp
// Round n UP to the next multiple of a power-of-two `align`.
inline std::size_t align_up(std::size_t n, std::size_t align) {
    assert(is_pow2(align));
    return (n + (align - 1)) & ~(align - 1);
}
```

Worked example, `align = 16` (binary `…10000`, so `align-1 = …01111`):

```
 n = 17 = 0b10001
 n + 15 = 32 = 0b100000
 ~(15)  = …11110000
 32 & ~15 = 32   →  align_up(17,16) = 32   ✓  (17 rounds up to 32)
 align_up(16,16) = 16  (already aligned, unchanged)
```

### The subtle bug: align the *address*, not the *offset*

An arena tracks a `head_` **offset** into its buffer. The naive move is
`align_up(head_, align)` — but the pointer the caller gets is `base_ + offset`, and
`base_` (from `malloc`) is only guaranteed **16-byte** aligned. If someone asks for
`align = 64`, aligning the *offset* to 64 doesn't make `base_ + offset` a multiple of
64 unless `base_` happened to be. So we align the **absolute address**:

```cpp
void* p = align_ptr(base_ + head_, align);     // align the real pointer
head_   = (uint8_t*)p - base_ + size;          // fold the padding back into head_
```

`align_ptr` is `align_up` on a `uintptr_t`. This is exactly the bug the code review
caught in subsystem A — the tests now check a 64-byte allocation lands 64-aligned.

## 4. Own or borrow the backing memory

Each allocator can either:

- **Own** its buffer: the constructor takes a byte count, `malloc`s once, and frees
  it in the destructor (RAII → no leaks). Backing-`malloc` failure is *fatal*
  (`oom_fatal` aborts loudly) — returning a half-built allocator with a null buffer
  would just crash later on first use (and in release, `assert` is compiled out).
- **Borrow** a buffer the caller already has (`void* + size`, non-owning). This lets
  allocators **nest** — e.g. carve a Pool out of an Arena's memory — and lets you put
  an allocator over a stack array with zero heap use.

```cpp
mem::Arena owns(64 * 1024);                 // mallocs 64 KiB, frees it on destruction
alignas(16) unsigned char buf[4096];
mem::Arena borrows(buf, sizeof buf);        // uses your buffer, frees nothing
```

## 5. Out of space is a *result*, not a crash

When an allocator can't satisfy a request it returns **`nullptr`** — a normal,
checkable outcome, not an assertion failure. (Asserting would abort in debug and make
the contract untestable; out-of-space is often a legitimate "try a bigger budget"
signal.) Callers check the pointer, exactly like `assets::load_file` returns an empty
optional:

```cpp
void* p = pool.allocate();
if (!p) { /* pool exhausted — grow it, or drop the request */ }
```

Programmer *mistakes* (non-power-of-two alignment, freeing out of LIFO order, freeing
a foreign pointer, double-free) **do** assert in debug — those are bugs, not results.

## 6. Stats & debug fill

Every allocator tracks a little accounting: `used()`, `capacity()`, `peak()` (the
high-water mark — invaluable for sizing budgets), and `count()` where it applies.
Near-zero cost, and the `peak()` tells you "this arena never needed more than 40 KiB"
so you can size it tightly.

An opt-in poison fill (compile with `MEM_DEBUG_FILL`) writes `0xCD` over freshly
allocated bytes and `0xDD` over freed bytes, so uninitialized reads and
use-after-free show up as obvious garbage in a debugger. Off by default (zero cost).

## 7. Pitfalls

- **Forgetting to destruct.** Allocators free *bytes*, not *objects*. If your type
  has a non-trivial destructor, call it yourself before reclaiming the memory.
- **Aligning the offset, not the address.** See §3 — the classic over-16 alignment
  bug.
- **Assuming `nullptr` can't happen.** Always check the return; budgets are finite.
- **Holding a pointer across a reset/rewind/flip.** Once you reclaim memory, every
  pointer into it is dangling. The later chapters call this out per allocator.

## 8. Glossary

- **Alignment** — the requirement that an object's address be a multiple of
  `alignof(T)`; misalignment is UB.
- **Arena / pool / stack / free-list / frame** — the five allocation strategies
  (chapters 35–37).
- **Own vs borrow** — the allocator `malloc`s its buffer vs uses the caller's.
- **High-water mark (`peak`)** — the most memory ever in use; sizes budgets.
- **Placement-new** — constructing an object into memory you already have.

## 9. Exercises

1. **Prove the alignment bug.** Temporarily change `align_ptr` to align the offset
   instead of the address, build with a 64-byte request, and watch the test fail.
2. **Size a budget.** Add `peak()` logging to a scene, run it, and shrink an arena to
   just above its peak. Confirm it still works.
3. **Poison hunt.** Build with `-DMEM_DEBUG_FILL`, intentionally read freed arena
   memory, and find the `0xDD` bytes in a debugger.
4. **Why power-of-two?** Work out what `align_up` does for `align = 24` (not a power
   of two) and explain why the `is_pow2` assert exists.

*(Next: chapter 35 — the linear family, Arena & Stack.)*
