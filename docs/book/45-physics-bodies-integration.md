# Chapter 45 — Bodies & Integration

> **What this is.** Subsystem **E**, part one: what a physics body is and how it moves
> under forces over time (**integration**). We keep it 2D and **linear only** (no
> rotation) so the math stays correct and readable. Collision is chapters 46–47.
> Code: `src/engine/physics/{shapes,body,world}.hpp`, `world.cpp`.

---

## 1. What a rigid body is

A rigid body is a thing with a position, a velocity, a mass, and a shape:

```cpp
struct Body {
    math::vec2 pos{0,0};
    math::vec2 vel{0,0};
    float      inv_mass    = 1.0f;   // 0 => static (infinite mass, immovable)
    float      restitution = 0.2f;   // bounciness (ch47)
    Shape      shape       = circle(0.5f);
};
```

Two shapes are enough for a convincing demo: a **Circle** (radius) and an axis-aligned
**Box** (half-extents).

```cpp
Shape circle(float r);        Shape box(math::vec2 half);
```

## 2. Why **inverse** mass

The body stores `inv_mass` = 1/mass, not mass. Three reasons:

1. The impulse math (ch47) divides by mass repeatedly; storing the reciprocal turns
   divisions into multiplications.
2. **Static objects fall out for free.** A floor or wall has *infinite* mass — pushing
   it does nothing. `1/∞ = 0`, so `inv_mass == 0` means "never accelerates". Every
   formula `Δv = impulse · inv_mass` automatically yields zero for static bodies; no
   special-casing.
3. It avoids dividing by zero for immovable objects.

```cpp
Body make_body  (vec2 pos, Shape s, float mass=1, float e=0.2);  // inv_mass = 1/mass
Body static_body(vec2 pos, Shape s, float e=0.2);                // inv_mass = 0
```

## 3. Integration: turning acceleration into motion

Gravity is an acceleration (`g`). To move a body we **integrate**: update velocity from
acceleration, then position from velocity, each frame by the timestep `h = dt`. We use
**semi-implicit (symplectic) Euler** — velocity *first*, then position with the *new*
velocity:

```cpp
b.vel = b.vel + gravity * h;   // 1) velocity from acceleration
b.pos = b.pos + b.vel  * h;    // 2) position from the UPDATED velocity
```

Why semi-implicit and not "explicit" Euler (position from the *old* velocity)? Explicit
Euler injects energy and blows up oscillating systems; semi-implicit is stable and
energy-conserving enough for games, for the same one line of code. It's the standard
game-physics integrator.

Static bodies are skipped entirely (they don't move):

```cpp
for (Body& b : bodies_) {
    if (!b.dynamic()) continue;   // inv_mass == 0
    b.vel = b.vel + gravity_ * h;
    b.pos = b.pos + b.vel  * h;
}
```

## 4. The coordinate convention: +y is down

The renderer's framebuffer has +y pointing **down** (ch02). To keep physics and
rendering consistent without flips, the physics world uses the same convention, so
default gravity is `(0, +9.8)` — "down" is increasing y. Pick a convention once and
hold it everywhere; mixing them is a classic source of upside-down bugs.

## 5. Worked example: free fall

`g = (0,10)`, a body starting at rest at `y=0`, stepping at `1/60 s`:

```
step 1: vel.y = 0 + 10·(1/60) = 0.1667;  pos.y = 0 + 0.1667·(1/60) = 0.00278
step 2: vel.y = 0.1667 + 0.1667 = 0.3333; pos.y += 0.3333·(1/60) = 0.00556 → 0.00833
…after ~1 s: vel.y ≈ 10 (≈ g·t), pos.y ≈ a few units down
```

The `test_integration` test asserts exactly this: after 60 steps the body has fallen
(`pos.y > 0`) and `vel.y ≈ g·t`.

## 6. Pitfalls

- **Explicit Euler.** Updating position with the *old* velocity is unstable; do
  velocity first (semi-implicit).
- **Storing mass instead of inverse mass.** You'll special-case statics and risk
  divide-by-zero. Store `inv_mass`; `0` = static.
- **Mixed y conventions.** Decide +y once (down, here) for both physics and rendering.
- **Variable dt.** Physics is most stable with a *fixed* timestep (ch03's accumulator);
  feed `step()` a constant `dt`.

## 7. Glossary

- **Rigid body** — pos + vel + mass + shape (no deformation; here, no rotation either).
- **Inverse mass** — 1/m; `0` encodes infinite mass = static/immovable.
- **Integration** — advancing velocity/position over a timestep.
- **Semi-implicit (symplectic) Euler** — velocity-then-position; stable for games.
- **Restitution** — bounciness, 0..1 (used in ch47).

## 8. Exercises

1. **Drop test.** Spawn a body, step 60×, and print `pos.y`/`vel.y`. Confirm
   `vel.y ≈ g·t`.
2. **Explicit Euler.** Swap the two integration lines (position from old velocity) and
   watch a bouncing scene gain energy. Revert.
3. **Per-body gravity scale.** Add a `gravity_scale` to `Body` (balloons float with a
   negative scale).
4. **Damping.** Multiply velocity by `0.99` each step and observe motion slowly dying —
   a cheap drag model.

*(Next: chapter 46 — collision detection.)*
