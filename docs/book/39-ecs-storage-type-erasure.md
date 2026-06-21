# Chapter 39 — Sparse-Set Storage & Type Erasure

> **What this is.** How the ECS stores components: one **sparse set** per component
> type (the generalized version of chapter 28's `Pool<T>`), and how the registry holds
> *many different component types* without naming them — **type erasure**. Code:
> `src/engine/ecs/sparse_set.hpp`, `registry.hpp`.

---

## 1. The sparse set, recalled

A sparse set (ch28 §4) stores one component type in three arrays:

```
 data_    [P0][P1][P2]          packed components — iterate THIS
 owners_  [ 5][ 2][ 9]          slot → which entity index owns it
 sparse_  [..,2,..,..,..,0,..,1] entity index → slot (or kTombstone)
           idx2↑       idx5↑ idx9↑
```

`add`/`get`/`has`/`remove` are all O(1); `remove` is **swap-and-pop** (move the last
slot into the hole, fix its owner's sparse entry). The generalization from ch28 is
that `SparseSet<T>` is now a template usable for *any* `T`, and it is keyed by the
entity **index** (the registry validates the generation before ever touching a pool).

```cpp
void SparseSet<T>::remove(std::uint32_t index) {
    if (!has(index)) return;
    const std::uint32_t slot = sparse_[index];
    const std::uint32_t last = static_cast<std::uint32_t>(data_.size()) - 1u;
    if (slot != last) {                       // fill the hole with the last element
        data_[slot]            = std::move(data_[last]);
        owners_[slot]          = owners_[last];
        sparse_[owners_[slot]] = slot;        // repoint the moved entity
    }
    data_.pop_back(); owners_.pop_back();
    sparse_[index] = kTombstone;
}
```

## 2. The problem: one registry, many component types

The farm ECS hard-coded its pools: `Pool<Position>`, `Pool<Renderable>`,
`Pool<Mover>` — three named members. An engine-core ECS can't do that; game code will
invent component types we've never heard of. The registry must hold an open-ended set
of `SparseSet<T>` for *arbitrary* `T`, yet expose them through one uniform interface.
That is **type erasure**.

## 3. Type erasure via a base interface + a per-type id

### The erased interface

A non-template base `IPool` exposes just the operations the registry needs *without*
knowing `T`:

```cpp
class IPool {
public:
    virtual ~IPool() = default;
    virtual bool        has(std::uint32_t index) const = 0;
    virtual void        remove(std::uint32_t index)    = 0;   // used by destroy()
    virtual std::size_t size() const                   = 0;
    // (non-copyable/movable — always owned via unique_ptr<IPool>)
};
template <typename T> class SparseSet : public IPool { /* …concrete… */ };
```

Now `Registry::destroy(e)` can drop every component of an entity by walking all pools
and calling the virtual `remove(index)` — no template needed:

```cpp
for (auto& p : pools_) if (p && p->has(e.index)) p->remove(e.index);
```

### Locating a type's pool: `type_id<T>()`

Each component type needs a stable slot in the registry's `vector<unique_ptr<IPool>>`.
We mint one with a function-local static counter — the first time `type_id<T>()` is
called for a given `T`, it grabs the next integer and remembers it forever:

```cpp
inline std::size_t next_type_id() { static std::size_t c = 0; return c++; }
template <typename T> std::size_t type_id() {
    static const std::size_t id = next_type_id();   // assigned once per T
    return id;
}
```

`add<T>` looks up (or lazily creates) `pools_[type_id<T>()]`:

```cpp
template <typename T> SparseSet<T>& pool() {
    const std::size_t id = type_id<T>();
    if (id >= pools_.size()) pools_.resize(id + 1);
    if (!pools_[id]) pools_[id] = std::make_unique<SparseSet<T>>();
    return *static_cast<SparseSet<T>*>(pools_[id].get());   // safe: id ⇒ this exact T
}
```

The `static_cast<SparseSet<T>*>` is sound because `type_id<T>()` is a bijection
between types and slots — slot `id` *always* holds a `SparseSet<T>` for that one `T`.

## 4. The typed API ties it together

```cpp
template<class T> T&   add(Entity e, T v = {});  // asserts valid(e); pool<T>().add(...)
template<class T> T*   get(Entity e);            // nullptr if invalid or absent
template<class T> bool has(Entity e) const;
template<class T> void remove(Entity e);
```

Every one first checks `valid(e)` (except `add`, which asserts it) so a stale handle is
safe. `get`/`has`/`remove` use the *non-creating* `pool_ptr<T>()` (returns nullptr if
that component type was never added) so querying an unknown component is harmless.

## 5. Pitfalls

- **Dangling references from `get()`.** It returns a pointer into a `std::vector`; a
  later `add<T>()` of the same type can reallocate and dangle it. Re-fetch; don't cache
  across `add`s. (Documented right at `get`.)
- **`static_cast` to the wrong type.** Only valid because `type_id<T>` ↔ slot is a
  strict bijection. Don't hand-store pools at arbitrary indices.
- **`type_id` across binaries.** The counter is per process image — fine for one
  executable, but don't persist ids to disk or share across DLLs.
- **Slicing the base.** `IPool` is non-copyable/non-movable so a concrete pool can't be
  accidentally sliced.

## 6. Glossary

- **Type erasure** — exposing many concrete types through one non-templated interface.
- **`type_id<T>()`** — a stable small integer per component type (function-local static).
- **Tombstone** — the sparse-array sentinel meaning "this index has no component here".
- **Swap-and-pop** — O(1) removal that keeps the dense array packed (order not preserved).

## 7. Exercises

1. **Add `clear<T>()`.** Wipe a whole component pool. Which arrays reset?
2. **Component count.** Expose `count<T>()` via the pool's `size()`; assert it matches
   the number of entities that `has<T>`.
3. **Group storage.** Sketch how an *archetype* ECS would store components instead
   (entities grouped by their exact component set). What gets faster, what gets harder?
4. **Back it with subsystem A.** Replace a `SparseSet<T>`'s `data_` vector with a
   fixed-capacity `mem::PoolAllocator`-backed array for a known-max component; measure.

*(Next: chapter 40 — views, systems, and the B acceptance.)*
