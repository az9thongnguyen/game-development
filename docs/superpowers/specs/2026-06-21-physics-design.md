# Subsystem E — 2D Physics Engine — Design Spec

> Date: 2026-06-21 · Program A→F, step E · Branch `feat/physics`
> Uses engine math (vec2). Standalone, deterministic, single-threaded core; adoption
> of ECS/jobs/frame-allocator documented as notes, not forced.

## 1. Goal & scope

A small, correct **2D rigid-body** physics engine: bodies fall under gravity, collide,
and bounce. Scope chosen to be the right size for a hand-written teaching engine:

- **Linear dynamics only** (position + velocity; no rotation/torque/inertia) — keeps
  the math tractable and correct. Rotation + friction are documented extensions.
- **Shapes:** Circle and axis-aligned Box (AABB).
- **Pipeline per step:** integrate (semi-implicit Euler + gravity) → broadphase (pairs)
  → narrowphase (contact manifolds) → resolve (impulse + positional correction).

Alternatives considered: a 3D engine (GJK/EPA narrowphase, constraint solver) —
rejected as far too large/risky for this milestone; rotation — deferred (needs inertia
tensors + angular impulse). Broadphase is O(n²) pair generation for clarity;
sweep-and-prune / spatial hash are exercises.

## 2. Files

```
src/engine/physics/   (namespace phys)
  shapes.hpp        Shape { Circle{radius} | Box{half_extents} }  (header-only)
  body.hpp          Body { pos, vel, inv_mass(0=static), restitution; Shape }  (header-only)
  collision.hpp/.cpp  Manifold + detect(): circle-circle, box-box, circle-box
  world.hpp/.cpp    World: add/iterate bodies, step(dt) running the full pipeline
tests/test_physics.cpp
docs/book/45,46,47 (split)
```

CMake: `physics_core` static lib (collision.cpp, world.cpp) PUBLIC include `src`;
`test_physics` links it. Pure, no SDL.

## 3. Types

```cpp
namespace phys {
enum class ShapeType { Circle, Box };
struct Shape { ShapeType type; float radius; math::vec2 half; };   // one or the other
Shape circle(float r);  Shape box(math::vec2 half);

struct Body {
    math::vec2 pos, vel;
    float      inv_mass    = 1.0f;   // 0 => static / infinite mass (immovable)
    float      restitution = 0.2f;   // 0 = inelastic, 1 = perfectly elastic
    Shape      shape;
    bool dynamic() const { return inv_mass > 0.0f; }
};

struct Manifold { int a, b; math::vec2 normal; float penetration; bool hit; };
}
```

## 4. Collision (narrowphase) — `collision.{hpp,cpp}`

`Manifold detect(const Body& a, const Body& b)` dispatched by shape pair; `normal`
points from a → b, `penetration` ≥ 0 on hit:

- **circle-circle:** centers distance vs sum of radii; normal = normalized delta
  (fallback (1,0) when coincident).
- **box-box (AABB):** overlap on both axes; choose the axis of least penetration for
  the normal.
- **circle-box:** closest point on the box to the circle center; hit if distance <
  radius; handles the circle-center-inside-box case.

## 5. World step — `world.{hpp,cpp}`

```cpp
class World {
    std::vector<Body> bodies_;
    math::vec2 gravity_{0, 9.8f};        // +y is down (screen convention)
public:
    int  add(const Body& b);             // returns index
    Body& body(int i); const Body& body(int i) const;
    int  count() const;
    void set_gravity(math::vec2 g);
    void step(double dt, int iterations = 4);
};
```

`step(dt)`:
1. **Integrate** dynamic bodies: `vel += gravity*dt; pos += vel*dt` (semi-implicit
   Euler — stable for games).
2. **Broadphase:** all i<j pairs (O(n²); documented).
3. **Narrowphase:** `detect` each pair → collect hits.
4. **Resolve** (×`iterations` for stability): for each manifold,
   - skip if separating (relative normal velocity ≥ 0),
   - impulse `j = -(1+e)·vn / (invA+invB)`, apply `±inv_mass·j·n`,
   - **positional correction** (Baumgarte): push apart by
     `max(pen−slop,0)/(invA+invB)·percent·n` (slop 0.01, percent 0.2) to stop sinking.

Two static bodies (invA+invB == 0) are skipped (no division by zero).

## 6. Correctness focus
- Detection: correct hit/miss, normal direction (a→b), penetration ≥ 0; the
  circle-inside-box and coincident-centers edge cases don't divide by zero.
- Static bodies (inv_mass 0) never move; static-static pairs skipped.
- Impulse: a head-on elastic (e=1) collision of equal masses exchanges velocity; an
  inelastic one (e=0) removes approach velocity.
- Resting: a ball dropped onto a static floor settles (penetration corrected, y
  stabilizes) over many steps — no explosion, no tunneling at modest speeds.
- Determinism: same inputs → same outputs (float, single-threaded).

## 7. Tests (`tests/test_physics.cpp`)
shape detection (circle-circle, box-box, circle-box: hit/miss, normal, penetration,
edge cases); integration (free fall distance after N steps); static immovability;
elastic vs inelastic 1-D impulse; a ball settling on a floor (y converges, stays
above floor); separating pair not resolved. ASan+UBSan.

## 8. Guidebook (split)
- **45 — Bodies & integration:** rigid bodies, inverse mass, semi-implicit Euler, why
  +y is down, gravity.
- **46 — Collision detection:** shapes, the manifold, circle/box narrowphase math,
  edge cases.
- **47 — Impulse resolution & the world step:** relative velocity, restitution, the
  impulse formula, positional correction, the step pipeline, E acceptance + adoption
  notes (ECS bodies, parallel broadphase via jobs, contacts in a FrameAllocator).

## 9. Risks
- Tunneling at high speed (discrete collision) — documented; continuous collision is an
  exercise. Tests use modest speeds.
- Stacking stability without friction/rotation is limited — documented; resolution
  iterations + positional correction give acceptable resting.
- Float determinism is within one platform/build; not cross-platform bit-exact
  (documented).
