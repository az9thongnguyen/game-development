# Chapter 47 — Impulse Resolution & the World Step

> **What this is.** Subsystem **E**, part three: making colliding bodies **bounce
> apart** correctly (impulse resolution), keeping them from sinking into each other
> (positional correction), and the full per-step pipeline that ties bodies (ch45) and
> detection (ch46) together. Then the E acceptance. Code:
> `src/engine/physics/world.cpp`.

---

## 1. The idea: change velocity with an impulse

When two bodies collide we apply an **impulse** — an instantaneous change in velocity —
along the contact normal, sized so they separate with the right bounciness. Working in
*velocity* (not force) avoids tiny-timestep instability.

Let `n` be the contact normal (a → b), `rv = b.vel − a.vel` the relative velocity, and
`vn = rv · n` the relative speed *along the normal* (negative = approaching). The
impulse scalar is:

```
        -(1 + e) · vn
  j  = ─────────────────
        invMassA + invMassB
```

- `e` is **restitution** (0 = no bounce, 1 = perfectly elastic).
- The denominator shares the impulse between the two bodies by inverse mass — a light
  body gets most of the velocity change; an infinite-mass (static) body gets none.

Apply it split by inverse mass, in opposite directions:

```cpp
vec2 impulse = n * j;
A.vel = A.vel - impulse * A.inv_mass;   // a recoils against the normal
B.vel = B.vel + impulse * B.inv_mass;   // b is pushed along it
```

Two guards:

- **Separating contact:** if `vn > 0` the bodies are already moving apart — skip (don't
  suck them back).
- **Two statics:** if `invMassA + invMassB == 0`, skip (and avoid dividing by zero).

### Worked check (equal masses, head-on, `e = 1`)

A moving `+1`, B moving `−1`, `n = (1,0)`: `vn = −2`, `j = −2·(−2)/2 = 2`,
`impulse = (2,0)`. A becomes `1 − 2 = −1`, B becomes `−1 + 2 = +1`. The velocities
**swap** — exactly what an elastic collision of equal masses does. (`test_elastic_exchange`.)

## 2. Restitution slop: letting things rest

A subtle bug (caught in review): a ball with `e > 0` resting on a floor never settles —
positional correction nudges it up a hair, restitution bounces that back, forever. The
standard fix is a **restitution threshold**: only apply bounce above a small approach
speed; below it, treat `e` as 0.

```cpp
constexpr float kRestThreshold = 1.0f;
float e = (vn < -kRestThreshold) ? min(A.restitution, B.restitution) : 0.0f;
```

Now a fast impact bounces, but a body slowly pressing under gravity comes to rest.
(`test_bouncy_ball_settles` covers an `e = 0.5` ball settling on a floor.)

## 3. Positional correction: un-sinking

Impulses fix *velocity*, but bodies that are already overlapping need their *positions*
nudged apart, or they slowly sink (gravity keeps pushing, impulses only zero the
velocity). We apply **Baumgarte** positional correction after the velocity pass:

```cpp
constexpr float slop = 0.01f, percent = 0.2f;
float corr = max(penetration - slop, 0) / (invA + invB) * percent;
vec2  push = n * corr;
A.pos = A.pos - push * A.inv_mass;
B.pos = B.pos + push * B.inv_mass;
```

- `slop` ignores tiny, harmless overlaps (prevents jitter).
- `percent` corrects only a fraction per frame (a full correction would pop violently);
  over a few frames the overlap melts away smoothly.

## 4. The step pipeline

`World::step(dt)` runs the whole thing in order:

```
1. integrate   — vel += g·dt; pos += vel·dt   (dynamic bodies; ch45)
2. broadphase  — gather candidate pairs (here O(n²); skip static-static)
3. narrowphase — detect() each pair → contact manifolds (ch46)
4a. resolve    — sequential impulses, `iterations` passes (stabler with shared contacts)
4b. correct    — Baumgarte positional correction
```

Why **iterate** the impulse pass? For a single isolated contact one pass suffices. But in
a *stack* (box on box on floor), resolving the bottom contact changes the velocity at
the contact above it; a few passes let the solution propagate. (More passes = stabler
stacks, more cost — `iterations` defaults to 4.)

```cpp
void World::step(double dt, int iterations) {
    /* 1 */ for (Body& b : bodies_) if (b.dynamic()) { b.vel += gravity_*h; b.pos += b.vel*h; }
    /* 2,3 */ contacts = detect_all_pairs();
    /* 4a */ for (it < iterations) for (c : contacts) resolve_velocity(c);
    /* 4b */ for (c : contacts) positional_correction(c);
}
```

## 5. E acceptance

- [x] **Bodies fall & integrate** correctly (semi-implicit Euler; `v ≈ g·t`).
- [x] **Detection**: circle/box manifolds with correct normal (a → b) + penetration,
      including circle-inside-box and coincident centers.
- [x] **Impulse resolution**: elastic exchange (e=1) swaps velocities; inelastic (e=0)
      removes approach velocity; statics never move; separating contacts skipped.
- [x] **Resting**: a ball settles on a floor — with `e = 0` *and* `e = 0.5` (restitution
      slop) — no tunneling at modest speed.
- [x] **Tests + safety**: `ctest physics` green; ASan+UBSan clean; warning-clean; no SDL.

Verified by `tests/test_physics.cpp` (detection, integration, static immovability,
elastic/inelastic impulse, and two settling tests).

## 6. Adoption notes (subsystems A–D)

- **ECS (B):** make `Body` a component and run `step` as a system over a `view<Body>()`.
- **Jobs (C):** the broadphase pair test and the integrate loop are embarrassingly
  parallel — `parallel_for` them (resolution stays serial for determinism).
- **Allocators (A):** the per-step `contacts` vector is ideal for a `FrameAllocator`
  (transient, thrown away each step) — the physics adoption promised in ch37.

## 7. Honest limitations

- **Discrete collision** → fast bodies can tunnel through thin geometry. Continuous
  collision (swept tests) is an exercise.
- **No rotation/friction** → boxes don't topple or grip; stacking is only as stable as
  linear resolution allows. Rotation needs inertia + angular impulse (a big extension).
- **O(n²) broadphase** → fine for hundreds of bodies; thousands want sweep-and-prune or
  a spatial hash (an exercise).

## 8. Glossary

- **Impulse** — an instantaneous change in momentum/velocity.
- **Restitution (e)** — bounciness, 0..1; **restitution slop** = threshold below which
  e is treated as 0 so contacts rest.
- **Positional / Baumgarte correction** — nudging overlapping bodies apart over frames.
- **Sequential impulses** — resolving contacts one at a time, iterated, for stacks.

## 9. Exercises

1. **Box stack.** Stack three boxes on a floor; tune `iterations` until they're stable.
2. **Bounce gallery.** Drop circles with `e` = 0, 0.5, 0.9 and watch the bounce heights.
3. **ECS integration.** Store `Body` as an ECS component and run physics as a system.
4. **Parallel broadphase.** `parallel_for` the pair generation with the job system and
   confirm identical results.

*(Subsystem E complete. Next: F — editor support, the last subsystem.)*
