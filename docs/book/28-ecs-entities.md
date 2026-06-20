# Chapter 28 — A Small Entity-Component-System

> **What this is.** The data architecture for everything that *isn't* the floor:
> trees, rocks, the house, and the walking farmer. We build a genuine but minimal
> **sparse-set ECS** in `src/games/iso/ecs.hpp` — small enough to read in one sitting,
> real enough to teach why big games store data this way. You will learn what
> entities, components, and systems actually are, how a sparse set gives O(1)
> add/remove/lookup, and why `remove()` does a "swap-and-pop".

---

## 1. The problem with fat game objects

The obvious way to model a tree or a farmer is a class:

```cpp
struct GameObject {
    float x, y;
    Sprite sprite;
    std::vector<Vec2i> path;   // only the farmer uses this
    float speed;               // only the farmer uses this
    bool  blocks;
    // …and 20 more fields as the game grows
};
std::vector<GameObject> objects;
```

This works for ten objects and rots for ten thousand. Every object pays memory for
fields it doesn't use (the rock carries an empty `path`). Iterating "all positions"
drags entire fat objects through cache. Adding a behavior means editing the one giant
class that everything depends on. This is the **fat-object** anti-pattern.

## 2. The ECS idea

An **Entity-Component-System** splits that fat object into three separate concepts:

- **Entity** — just an id. A `uint32_t`. It owns nothing; it *names* a thing.
- **Component** — a plain data struct (`Position`, `Renderable`, `Mover`). Stored in
  its own array, apart from the entity.
- **System** — a function that walks one or more component arrays and does work
  (move all movers; draw all renderables). Logic lives here, not in the data.

The win: data of one kind is **contiguous** (cache-friendly), entities are cheap
integers, and a new behavior is a new component pool + a new system — no edits to
existing types. An entity simply *has* the components it needs:

```
Entity 1 (a tree)   → Position, Renderable
Entity 2 (a rock)   → Position, Renderable
Entity 7 (farmer)   → Position, Renderable, Mover
```

The farmer is the only thing with a `Mover`, so only the farmer pays for a path.

## 3. The components

```cpp
struct Position   { float x = 0.0f, y = 0.0f; };          // grid coords (fractional ok)
struct Renderable { ObjKind kind = ObjKind::Tree; gfx::Color tint = 0xFFFFFFFFu; };
struct Mover {
    std::vector<Vec2i> path;       // A* result, path[idx] is the next cell
    std::size_t        idx   = 0;
    float              speed = 3.0f;  // tiles per second
    bool moving() const { return idx < path.size(); }
};
```

Components are *pure data* — no virtual functions, no behavior. `Renderable` carries
an `ObjKind` (tree/rock/house/fence/wheat/farmer; Chapter 27 §4 draws each) and a
`tint` left at white for M4. `Mover` is the walking state the movement system reads.

## 4. The sparse set: O(1) everything

Each component type gets its own `Pool<T>`. A pool must support, all in O(1):
`has(e)`, `add(e, v)`, `get(e)`, `remove(e)`, and *fast iteration* over all
components. The data structure that delivers this is the **sparse set**:

```cpp
template <typename T>
class Pool {
    std::vector<Entity>                     dense_;  // dense_[i] owns data_[i]
    std::vector<T>                          data_;   // packed components
    std::unordered_map<Entity, std::size_t> at_;     // entity -> slot i
};
```

Three parallel structures:

- **`data_`** holds the components *packed* with no gaps → iteration is a tight loop
  over a contiguous vector (the ECS payoff).
- **`dense_`** is the same length; `dense_[i]` records which entity owns `data_[i]`.
- **`at_`** is the "sparse" half: it maps an entity id to its slot `i`. (A real engine
  uses a sparse *array* indexed by entity id; we use a hash map, which is simpler and
  plenty fast for a farm.)

```
   at_:  {1→0, 2→1, 7→2}          (entity → slot)
   dense_: [ 1 ,  2 ,  7 ]        (slot → entity)
   data_:  [P1 , P2 , P7 ]        (slot → component)   ← iterate THIS contiguously
```

### add / get / has

```cpp
T& add(Entity e, const T& v) {
    auto it = at_.find(e);
    if (it != at_.end()) { data_[it->second] = v; return data_[it->second]; } // overwrite
    at_[e] = data_.size();          // new slot = current end
    dense_.push_back(e);
    data_.push_back(v);
    return data_.back();
}
T* get(Entity e) { auto it = at_.find(e); return it == at_.end() ? nullptr : &data_[it->second]; }
bool has(Entity e) const { return at_.count(e) != 0; }
```

`get` returns a pointer so "no such component" is a clean `nullptr`, not an exception
or a sentinel — the movement system uses exactly this: `if (!mv || !pos) return;`.

### remove: swap-and-pop

Removing slot `i` from a packed array can't just erase it — that would leave a hole
or shift everything down (O(n)). Instead we **move the last element into the hole**
and shrink by one. O(1), at the cost of not preserving order (callers must not rely on
iteration order):

```cpp
void remove(Entity e) {
    auto it = at_.find(e);
    if (it == at_.end()) return;
    const std::size_t i    = it->second;
    const std::size_t last = data_.size() - 1;
    if (i != last) {                       // move the last slot into the hole
        data_[i]       = std::move(data_[last]);
        dense_[i]      = dense_[last];
        at_[dense_[i]] = i;                // re-point the moved entity to slot i
    }
    data_.pop_back();
    dense_.pop_back();
    at_.erase(it);                         // drop e's mapping last
}
```

```
   remove entity 2 (slot 1), last is entity 7 (slot 2):

   before: dense_ [1, 2, 7]   data_ [P1, P2, P7]   at_ {1→0, 2→1, 7→2}
   step:   move slot 2 → slot 1, fix at_[7]=1
   after:  dense_ [1, 7]      data_ [P1, P7]       at_ {1→0, 7→1}
```

The `i != last` guard handles the case where the removed element *is* the last one
(nothing to move). `test_ecs` in `tests/test_iso.cpp` checks that after removing a
middle entity the survivors are still reachable and intact.

## 5. The World

`World` ties the pools together and hands out entity ids:

```cpp
class World {
    Entity              next_ = 1;       // 0 == kInvalid, never handed out
    std::vector<Entity> alive_;
public:
    Entity create() { Entity e = next_++; alive_.push_back(e); return e; }
    void   destroy(Entity e);            // removes e from ALL pools + alive_
    void   clear();                      // wipe everything; ids restart at 1
    Pool<Position>   positions;
    Pool<Renderable> renderables;
    Pool<Mover>      movers;
};
```

`destroy` calls `remove` on every pool, so deleting a tree cleans up its position and
renderable in one go. `clear` (used by save-load) resets the pools and restarts ids —
which is why a freshly loaded farm has deterministic entity numbers.

## 6. Systems are just functions

There is no `System` base class — a system is any function that operates over the
pools. The movement system lives in `Farm::update` (Chapter 29):

```cpp
Mover*    mv  = world_.movers.get(farmer_);
Position* pos = world_.positions.get(farmer_);
if (!mv || !pos || !mv->moving()) return;
// …advance pos toward mv->path[mv->idx]…
```

The render "system" lives in `iso_render.cpp`: it iterates `world.alive()`, pulls each
entity's `Position` + `Renderable`, and depth-sorts them (Chapter 27). Data in pools,
behavior in free functions — that separation *is* the ECS.

## 7. Run & observe

In `--iso`, the HUD shows `entities:N`. Place objects (left-click) and watch it climb;
bulldoze (`0`) and watch it fall — each `place_object`/`remove_object` is a
`World::create`/`destroy` under the hood. `R` resets: `World::clear` wipes the pools
and the count snaps back to the starter scene's total.

## 8. Pitfalls

- **Dangling pointers from `get`.** `add`/`remove` can reallocate `data_`, invalidating
  any `T*` you held. Fetch the pointer *after* any structural change — the editor
  scene comment in Chapter 25 made the same point for the 3D editor.
- **Assuming iteration order.** Swap-and-pop scrambles order. If you need stable
  order, sort at use time (the depth sort does) — don't rely on insertion order.
- **Entity 0.** `kInvalid = 0` is never created, so `0` is a safe "no entity" value.
  Don't start `next_` at 0.
- **Map vs. sparse array.** We used `unordered_map` for the sparse half for clarity.
  A production ECS uses a sparse *array* (entity id → slot) for true O(1) without
  hashing; that's the natural next step (exercise 3).

## 9. Glossary

- **Entity** — an integer id naming a thing; owns no data itself.
- **Component** — a plain data struct stored in a pool.
- **System** — a function that processes components (movement, rendering).
- **Sparse set** — packed `data_`/`dense_` arrays + a sparse entity→slot map giving
  O(1) add/remove/lookup and contiguous iteration.
- **Swap-and-pop** — O(1) removal by moving the last element into the freed slot.

## 10. Exercises

1. **A `Health` component + a system.** Add `struct Health { int hp; }`, a pool, and a
   `damage_system`. Notice you edited *nothing* that already existed — that's the ECS
   promise.
2. **Generational ids.** Pack a generation counter into the high bits of `Entity` so a
   stale id from a destroyed entity can't accidentally alias a recycled one.
3. **Sparse array.** Replace `unordered_map at_` with a `std::vector<std::size_t>`
   indexed by entity id. Measure iteration speed on 100k entities.
4. **View/query.** Write a helper that iterates only entities having *both* `Position`
   and `Mover` (the classic ECS "view"), and use it for the movement system.
