# Chapter 40 — Views, Systems & B Acceptance

> **What this is.** The payoff of the ECS: **views** that iterate every entity with a
> given set of components, **systems** as plain functions over those views, and the
> acceptance check that closes subsystem **B**. Code: `src/engine/ecs/registry.hpp`.

---

## 1. Views: "every entity that has A and B"

The whole point of an ECS is to ask "give me everything with a `Position` *and* a
`Velocity`" and run logic over it. That's a **view**:

```cpp
registry.view<Position, Velocity>([](ecs::Entity e, Position& p, Velocity& v) {
    p.x += v.x;
    p.y += v.y;
});
```

The callback fires once per entity that has *all* the listed components, receiving the
entity and a mutable reference to each component.

### How it works

A view needs the intersection of several pools. The simplest correct approach: pick one
pool to **drive** the loop, then for each of its entities, check the *other* pools also
have that entity:

```cpp
template <typename First, typename... Rest, typename Fn>
void view(Fn&& fn) {
    SparseSet<First>* driver = pool_ptr<First>();
    if (!driver) return;
    if (((pool_ptr<Rest>() == nullptr) || ...)) return;        // a required pool absent
    const std::vector<std::uint32_t> owners = driver->owners(); // copy: safe vs growth
    for (std::uint32_t i : owners) {
        if (!driver->has(i)) continue;
        if (((pool_ptr<Rest>()->has(i)) && ...)) {              // entity has all Rest too
            std::forward<Fn>(fn)(Entity{i, generations_[i]},
                                 *driver->get(i), *pool_ptr<Rest>()->get(i)...);
        }
    }
}
```

Two C++ details worth seeing:

- **Fold expressions.** `((pool_ptr<Rest>()->has(i)) && ...)` expands to
  `p0->has(i) && p1->has(i) && …` over the `Rest` pack — and for a *single*-component
  view (empty `Rest`) it collapses to the identity `true`, so `view<Position>` just
  visits every `Position`. Likewise `*pool_ptr<Rest>()->get(i)...` expands the extra
  reference arguments (none for a single-component view).
- **Drive by `First`.** We loop the first listed component's pool. So **list the
  rarest component first** — fewer driver entities means fewer membership checks.
  (Auto-selecting the smallest pool is a nice refinement; left as an exercise to keep
  the fold simple.)

### The reentrancy rule

The callback holds live references into the pools. If it *adds* a component of an
iterated type (possible vector reallocation) or *destroys/removes* the entity it's
processing (swap-and-pop moves data under the reference), those references dangle. So
the documented **precondition**: a view callback must not destroy/remove the entity it
receives or add components of the iterated types. Queue such structural changes and
apply them after the view returns. (We copy the driver's `owners` list up front, so
merely *creating new* entities during iteration is safe — they just aren't visited
this pass.)

## 2. Systems are just functions

There is no `System` base class. A "system" is any function that takes a `Registry&`
and runs a view — exactly the engine's style elsewhere (the iso movement/render
"systems" are free functions too):

```cpp
void movement_system(ecs::Registry& r, float dt) {
    r.view<Position, Velocity>([dt](ecs::Entity, Position& p, Velocity& v) {
        p.x += v.x * dt;
        p.y += v.y * dt;
    });
}
```

Data lives in pools; behavior lives in functions over views. Adding a behavior is a new
function; adding data is a new component type — neither touches existing code. That
decoupling *is* the ECS.

## 3. B acceptance

- [x] **Generic, any component type** — type-erased registry (`type_id<T>` + `IPool`),
      no hard-coded pools. (ch39)
- [x] **Safe entity handles** — index + generation; stale handles fail `valid()`;
      generation-wrap retires the slot. (ch38)
- [x] **add / get / has / remove** — O(1) sparse-set ops; swap-and-pop survivors
      intact; overwrite supported.
- [x] **destroy clears all components** — virtual `remove` across every pool; index
      recycled clean.
- [x] **views** — multi- and single-component, correct intersection, empty-pool safe.
- [x] **Tests + safety** — `ctest ecs` green; clean under **ASan+UBSan**; warning-clean;
      no SDL.

Verified by `tests/test_ecs.cpp`: entity recycling + stale invalidation, component
add/overwrite/remove + swap-and-pop, destroy-clears-all, multi/single/empty views, and
stale-handle no-op safety.

## 4. How B relates to ch28 (the farm ECS)

The farm ECS (ch28) stays exactly as it was — a focused, readable first contact with
the idea. Subsystem B is the **engine-core** version: generic, handle-safe, view-based.
New engine systems (physics in subsystem E, the editor's inspector in F) build on B;
the farm keeps its bespoke one as a teaching artifact. Same concept, two scales — which
was the lesson.

## 5. Glossary

- **View** — an iteration over all entities possessing a given set of components.
- **Fold expression** — `(pattern op ...)`; expands an operation over a template pack.
- **System** — a function that runs logic over a view (no base class).
- **Reentrancy** — calling back into the registry from inside a view; restricted to
  avoid dangling references.

## 6. Exercises

1. **`movement_system`.** Write it (as above), spawn 1000 entities with Position +
   Velocity, step it, and assert positions advanced.
2. **Deferred destroy.** Inside a view, collect entities to delete into a vector, then
   destroy them after — the safe pattern for the reentrancy rule.
3. **Smallest-pool driver.** Make `view` pick the smallest pool to drive automatically
   (hint: dispatch the loop body per type, guarded by a runtime "is this the min?").
4. **Exclude filter.** Add `view<Include...>().without<Exclude...>()` semantics that
   skip entities having any `Exclude` component.

*(Subsystem B complete. Next in the program: C — the job system.)*
