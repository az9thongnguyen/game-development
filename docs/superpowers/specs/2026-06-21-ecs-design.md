# Subsystem B — Engine-core ECS — Design Spec

> Date: 2026-06-21 · Program A→F, step B · Branch `feat/ecs`
> Builds on A (allocators). Generalizes the farm-specific sparse-set ECS of ch28
> into a reusable, type-erased entity-component-system for the whole engine.

## 1. Goal & what's different from ch28

The iso game already has a tiny ECS (`src/games/iso/ecs.hpp`): hard-coded component
pools (Position/Renderable/Mover), plain incrementing ids, farm-only. Subsystem B
makes a **generic, engine-core** ECS:

- **Any component type** (type-erased registry; no hard-coded pools).
- **Safe entity handles**: index **+ generation**, so a stale handle to a destroyed
  entity is detected (the bug ch28's plain ids can't catch).
- **Views/queries** over arbitrary component sets.
- Lives in `src/engine/ecs/`, namespace `ecs`. The iso ECS stays as-is (a teaching
  precedent); new code uses this one.

## 2. Decisions (with alternatives)

- **Storage: sparse-set** (per-component dense arrays + sparse index map), NOT
  archetype. Sparse-set is O(1) add/remove/has, simple, and continuous with ch28.
  Archetype iterates multi-component queries faster and is more cache-friendly but is
  far more complex (chunk management, moving entities between archetypes); noted as a
  future option / exercise.
- **Entity handle: `index + generation`** packed in a struct. On destroy, the index's
  generation is bumped and the index recycled; `valid(e)` checks the stored generation
  matches. Prevents use-after-free of entity ids.
- **Type erasure: a per-type id** from a function-local static counter
  (`type_id<T>()`), indexing a sparse `vector<unique_ptr<IPool>>`. Each `Pool<T>` is a
  `SparseSet<T>` behind an `IPool` interface (so the registry can `remove`/`has` a
  component by entity without knowing T).
- **Dense storage: `std::vector`** for growability (entity count isn't fixed).
  Allocator adoption from A is offered as an option (a fixed-capacity pool-backed
  variant) but not forced — contorting a growable generic store onto a fixed Pool
  would hurt clarity. The strongest A adoptions are physics (frame) and assets (arena).
- **Systems: free functions over views** (no System base class), matching engine style.

## 3. Components & files

```
src/engine/ecs/
  entity.hpp     Entity {index, generation} + ==, hash, null; (header-only)
  sparse_set.hpp SparseSet<T>: dense data[]/owners[] + sparse index[]; add/get/has/remove
                 (swap-and-pop), iteration; IPool base for type erasure (header-only)
  registry.hpp   Registry: entity create/destroy/valid (gen recycling) + typed
                 add/get/has/remove<T> + view<Cs...>; (header-only templates)
  registry.cpp   non-template bits: entity allocator (generations + free list),
                 type-erased pool vector, destroy()-removes-from-all-pools
tests/test_ecs.cpp   CTest 'ecs'
```

CMake: `ecs_core` static lib (registry.cpp) PUBLIC include `src`; `test_ecs` links it.
Pure, no SDL.

## 4. Key APIs

```cpp
struct Entity { std::uint32_t index = 0; std::uint32_t generation = 0; };
inline bool operator==(Entity, Entity);
inline constexpr Entity null_entity{0, 0};   // generation 0 = never valid

class Registry {
public:
    Entity create();
    void   destroy(Entity e);
    bool   valid(Entity e) const;
    std::size_t alive() const;

    template<class T> T&   add(Entity e, T v = {});
    template<class T> T*   get(Entity e);            // nullptr if absent/invalid
    template<class T> bool has(Entity e) const;
    template<class T> void remove(Entity e);

    // Iterate entities having ALL of Cs; callback gets (Entity, Cs&...).
    template<class... Cs, class Fn> void view(Fn&& fn);
};
```

`view` picks the **smallest** pool among `Cs...` to drive iteration, then checks the
other pools — the standard "iterate the rarest component" optimization.

## 5. Correctness focus

- **Generation handles**: create after destroy returns a handle whose generation
  differs from the stale one; `valid(stale) == false`; `get`/`has`/`add` on an invalid
  entity are safe no-ops/nullptr.
- **Sparse-set swap-and-pop**: removing a component keeps the dense array packed and
  fixes the moved entity's sparse slot (the ch28 logic, generalized + tested).
- **destroy removes from all pools**: no orphan components; index recycled.
- **view** yields exactly the entities having all requested components, with correct
  references; safe if a pool is empty.
- Index-keyed sparse sets use the entity **index** (generation validated at the
  registry layer before any pool access).

## 6. Tests (`tests/test_ecs.cpp`)

create/destroy/valid + generation invalidation + index recycling; add/get/has/remove
(incl. overwrite, swap-and-pop survivors); destroy removes all components; multi-type
view yields the right set with correct values; empty-pool view; stale-handle safety.
Run under ASan+UBSan.

## 7. Guidebook (split, small chapters)

- **38 — Entities & safe handles**: index+generation, recycling, why ch28's plain ids
  were unsafe.
- **39 — Sparse-set storage & type erasure**: the dense/sparse arrays, swap-and-pop,
  `type_id<T>` + `IPool`.
- **40 — Views, systems & acceptance**: querying component sets, the rarest-pool trick,
  systems as functions, B acceptance + the relationship to ch28.

Update overview + README.

## 8. Risks
- Type-erasure `type_id<T>` uses a static counter — fine single-binary; documented.
- std::vector storage (not A-backed) — deliberate (see §2); documented honestly.
- Keep the iso ECS untouched (no refactor churn); B is additive.
