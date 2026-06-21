# Chapter 38 — Entities & Safe Handles

> **What this is.** The start of subsystem **B**, the engine-core ECS. Chapter 28
> built a tiny, farm-specific ECS with plain integer ids. This chapter fixes that
> design's one real hazard — **stale handles** — with an **index + generation**
> handle, and explains entity recycling. Code: `src/engine/ecs/entity.hpp`,
> `registry.cpp`.

---

## 1. The stale-handle problem

Chapter 28's entity was a plain `uint32_t` that only ever counted up. That's fine
until you destroy and recreate:

```
create() → 7      // someone keeps the handle "7"
destroy(7)        // entity 7 is gone…
create() → 7?     // if the id is reused, "7" now names a DIFFERENT entity
```

The old holder of "7" can't tell. It reads/writes the new entity's components by
accident — a silent, miserable bug. (The farm ECS dodged it only because it never
recycled ids and never handed entity ids around for long.)

## 2. The fix: index + generation

An `Entity` is two numbers:

```cpp
struct Entity {
    std::uint32_t index      = 0;   // which slot
    std::uint32_t generation = 0;   // which "use" of that slot (0 = null)
};
```

The registry stores, per index, the **current** generation. Every handle remembers
the generation it was minted with. To destroy an entity, the registry **bumps** that
index's generation; any old handle now carries a generation that no longer matches, so
`valid()` rejects it:

```
 slot 7, generation 1   →  Entity{7,1} handed out
 destroy → slot 7 generation becomes 2
 create() reuses slot 7 →  Entity{7,2}
 valid({7,1}) == false   ← the stale handle is detectably dead
 valid({7,2}) == true
```

`valid()` is three cheap checks:

```cpp
bool Registry::valid(Entity e) const {
    return e.generation != 0
        && e.index < generations_.size()
        && generations_[e.index] == e.generation;
}
```

Generation `0` is reserved as "never valid", so `null_entity = {0,0}` and any
freshly-zeroed handle are safely invalid.

## 3. Recycling indices

Destroyed indices go onto a free list and are reused, so the `generations_` array
doesn't grow without bound:

```cpp
Entity Registry::create() {
    std::uint32_t index;
    if (!free_.empty()) { index = free_.back(); free_.pop_back(); }   // reuse
    else { index = generations_.size(); generations_.push_back(1); }  // grow (gen 1)
    ++alive_;
    return Entity{index, generations_[index]};
}

void Registry::destroy(Entity e) {
    if (!valid(e)) return;                 // double-destroy is a safe no-op
    /* …remove all components (ch39)… */
    const std::uint32_t next = generations_[e.index] + 1;
    if (next == 0) { generations_[e.index] = 0; /* retire, don't recycle */ }
    else           { generations_[e.index] = next; free_.push_back(e.index); }
    --alive_;
}
```

### Generation overflow → retire the slot

A generation is 32 bits. After ~4 billion destroy/create cycles of *the same slot* it
would wrap. If we wrapped back to 1, a brand-new handle could match a 4-billion-old
one — the classic **ABA** bug. So when a slot's generation is about to wrap, we
**retire** it: set generation `0` (always invalid) and never recycle it. We trade one
slot for guaranteed correctness. (This was a code-review catch — the first draft
wrapped to 1.)

## 4. Worked example

```cpp
ecs::Registry r;
ecs::Entity a = r.create();           // {0,1}
ecs::Entity b = r.create();           // {1,1}
r.destroy(a);                         // slot 0 → generation 2, freed
ecs::Entity c = r.create();           // reuses slot 0 → {0,2}
r.valid(a);  // false  (a is {0,1}, slot 0 is now gen 2)
r.valid(c);  // true
c.index == a.index;  // true (same slot)  — but different generation
```

## 5. Pitfalls

- **Comparing only the index.** Two handles are equal only if *both* index and
  generation match — `operator==` checks both.
- **Trusting a stored handle without `valid()`.** After anything might have destroyed
  it, check `valid(e)` (or just use `get<T>` which returns nullptr for invalid).
- **Assuming ids are dense/sequential.** Recycling means indices come back in free-list
  order, not 0,1,2,… Don't index external arrays by raw entity index without care.

## 6. Glossary

- **Handle** — a value that names a resource indirectly (here, index + generation).
- **Generation** — a per-slot counter bumped on destroy to invalidate old handles.
- **Recycling** — reusing a freed index for a new entity.
- **ABA problem** — a value returns to a previous state (here a wrapped generation),
  fooling a stale reference into looking current.

## 7. Exercises

1. **Force a stale hit.** Temporarily make `destroy` *not* bump the generation; write a
   test that reads the wrong entity through a recycled handle. Then restore the bump.
2. **Pack into 64 bits.** Store the handle as one `uint64_t` (`gen<<32 | index`) and
   reimplement `valid`. What are the trade-offs vs the struct?
3. **Count retirements.** Add a counter for retired slots and a test that drives one
   slot's generation to wrap (shrink generation to 8 bits first to make it feasible).

*(Next: chapter 39 — sparse-set component storage & type erasure.)*
