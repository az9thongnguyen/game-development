# Chapter 46 — Collision Detection

> **What this is.** Subsystem **E**, part two: deciding whether two bodies overlap and,
> if so, producing a **contact manifold** — the normal and penetration depth the solver
> needs to push them apart (ch47). We cover the three shape pairs (circle-circle,
> box-box, circle-box) and their edge cases. Code:
> `src/engine/physics/collision.{hpp,cpp}`.

---

## 1. The manifold: the output of detection

Detection answers three things, bundled in a `Manifold`:

```cpp
struct Manifold {
    bool       hit;          // do they overlap?
    math::vec2 normal;       // unit direction from a → b (the separation axis)
    float      penetration;  // how deep the overlap is (>= 0 on hit)
};
Manifold detect(const Body& a, const Body& b);   // dispatches on shape types
```

The **normal** is the direction to push them apart; the **penetration** is how far.
Convention: the normal points from `a` toward `b`. Everything downstream relies on
that, so detection must get the sign right.

## 2. Circle vs circle

Two circles touch when their centers are closer than the sum of radii:

```cpp
vec2  d  = b.pos - a.pos;                 // a → b
float r  = a.radius + b.radius;
if (length2(d) >= r*r) return {no hit};
float dist = length(d);
normal      = dist > 1e-6 ? d/dist : vec2{1,0};   // guard coincident centers
penetration = r - dist;
```

The `dist > 1e-6` guard avoids dividing by zero when centers coincide (we pick an
arbitrary unit normal `{1,0}` and full penetration). Using `length2` for the early-out
avoids a square root on the common no-hit case.

## 3. Box vs box (AABB)

Two axis-aligned boxes overlap when they overlap on **both** axes. The separation
normal is the axis of **least** penetration (the cheapest way out):

```cpp
vec2 d  = b.pos - a.pos;
float ox = (a.half.x + b.half.x) - fabs(d.x);   if (ox <= 0) return {no hit};
float oy = (a.half.y + b.half.y) - fabs(d.y);   if (oy <= 0) return {no hit};
if (ox < oy) { normal = {sign(d.x), 0}; penetration = ox; }   // separate on x
else         { normal = {0, sign(d.y)}; penetration = oy; }   // separate on y
```

`sign(d.x)` makes the normal point from a toward b. Choosing the least-penetration axis
is what makes a box resting on a floor get pushed *up* (small y overlap) rather than
sideways.

## 4. Circle vs box (the tricky one)

Find the **closest point on the box to the circle's center** by clamping the center
into the box's extents; the circle hits if that point is within `radius`:

```cpp
vec2 d       = circ.pos - bx.pos;                          // box-center → circle-center
vec2 closest = { clamp(d.x, -h.x, h.x), clamp(d.y, -h.y, h.y) };
```

Two cases:

- **Center outside the box** (`closest != d`): `to_c = d - closest` points from the box
  surface to the circle center; if `|to_c| < radius` it's a hit, with
  `penetration = radius - |to_c|` and the separation direction `to_c` normalized.
- **Center inside the box** (`closest == d`, i.e. the center is within the extents): the
  closest-point vector is zero, so we instead pop out along the **nearest face** —
  whichever axis has the smaller distance-to-face — with
  `penetration = radius + (half − |d|)` on that axis.

```cpp
bool inside = (d.x >= -h.x && d.x <= h.x && d.y >= -h.y && d.y <= h.y);
```

Finally the normal is flipped to the `a → b` convention. And because `detect()` always
passes the circle as the first arg internally, the **box-vs-circle** case computes
`circle_box(b, a)` and negates the normal so the result still points `a → b`:

```cpp
Manifold m = circle_box(b, a);   // normal b(circle) → a(box)
m.normal = -m.normal;            // → a → b
```

(The unit test checks exactly this flip: circle-vs-box gives `+x`, box-vs-circle `-x`.)

## 5. Pitfalls

- **Wrong normal sign.** Everything downstream assumes `a → b`. Test both orderings
  (`detect(a,b)` and `detect(b,a)`) — the normal must simply negate.
- **Forgetting the circle-inside-box case.** The closest-point method degenerates when
  the center is inside; handle it explicitly or you get a zero/garbage normal.
- **Dividing by zero.** Coincident centers / zero closest-vector need a guarded
  fallback normal.
- **Square roots everywhere.** Compare squared distances for the early-out; only take
  the root once you know there's a hit.

## 6. Glossary

- **Manifold / contact** — the result of detection: normal + penetration.
- **Normal** — unit separation direction (here, from a → b).
- **Penetration** — overlap depth to resolve.
- **AABB** — axis-aligned bounding box.
- **Closest-point** — the box point nearest the circle center; the basis of circle-box.

## 7. Exercises

1. **Symmetry test.** For each pair type assert `detect(a,b).normal == -detect(b,a).normal`.
2. **Circle through a corner.** Place a circle just off a box corner and verify the
   normal points diagonally (the closest point is the corner).
3. **Oriented boxes.** Sketch what changes to support rotated boxes (hint: the
   Separating Axis Theorem). Why is AABB so much simpler?
4. **Capsule.** Add a capsule (segment + radius) shape and its circle/capsule test.

*(Next: chapter 47 — impulse resolution & the world step.)*
