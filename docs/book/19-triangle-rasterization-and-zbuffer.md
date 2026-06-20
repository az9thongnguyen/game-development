# Chapter 19 — Triangle Rasterization & the Z-Buffer

> **Where we are.** Chapter 18 got a vertex from model space to a screen pixel
> `(x, y)` plus a depth in `[0,1]`. But a triangle has *three* vertices and covers
> *many* pixels between them. This chapter is about **filling** that triangle: which
> pixels are inside, what depth and color each one gets, and how nearer triangles
> correctly hide farther ones. This is the literal heart of the 3D renderer.

---

## 1. From three points to a filled shape

Given three screen points, we must color every pixel inside the triangle. There are
two classic approaches:

- **Scanline:** walk the triangle row by row, computing left/right edges per row.
- **Edge-function / barycentric:** for each pixel in the triangle's bounding box, ask
  "is this pixel inside?" using three edge tests.

We use the **edge-function** method. It is branch-simple, handles every triangle
orientation the same way, makes the *fill rule* easy, and — best of all — the numbers
it computes (*barycentric coordinates*) are exactly what we need to interpolate depth
and color. It is also how real GPUs rasterize.

---

## 2. The edge function

For a directed edge from `a` to `b`, the **edge function** evaluated at point `p` is:

```cpp
inline float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}
```

This is the z-component of the 2D cross product `(b−a) × (p−a)`. Two facts make it
powerful:

1. **Its sign tells you which side** of the line `a→b` the point `p` is on.
2. **Its magnitude is twice the area** of triangle `(a, b, p)`.

A point is **inside** triangle `(a, b, c)` when it is on the same side of all three
directed edges `a→b`, `b→c`, `c→a` — i.e. all three edge functions share a sign.

```
        c                For p inside, edge(a,b,p), edge(b,c,p),
       / \               and edge(c,a,p) all have the same sign.
      /   \              Flip one vertex order and all three flip
     / .p  \             together — that's the backface test (Ch. 22).
    a───────b
```

---

## 3. Barycentric coordinates

Normalize the three edge functions by the triangle's total area and you get
**barycentric weights** `(w0, w1, w2)` — how much each vertex "owns" the point:

```cpp
inline math::vec3 barycentric(math::vec2 a, math::vec2 b, math::vec2 c, math::vec2 p) {
    const float area = edge(a.x,a.y, b.x,b.y, c.x,c.y);
    if (area == 0.0f) return {-1,-1,-1};          // degenerate triangle
    const float inv = 1.0f / area;
    const float w0 = edge(b.x,b.y, c.x,c.y, p.x,p.y) * inv;  // weight of a
    const float w1 = edge(c.x,c.y, a.x,a.y, p.x,p.y) * inv;  // weight of b
    const float w2 = edge(a.x,a.y, b.x,b.y, p.x,p.y) * inv;  // weight of c
    return {w0, w1, w2};
}
```

They always **sum to 1**, and the point is inside exactly when **all three are ≥ 0**.
At the centroid each weight is `1/3` (that's `test_barycentric`). Any per-vertex value
— depth, color, a normal — can be blended at `p` by `w0·v0 + w1·v1 + w2·v2`.

---

## 4. The rasterizer loop

Putting it together (`renderer3d.cpp`, `raster_triangle`): compute the screen-space
bounding box, then test every pixel in it.

```cpp
for (int y = miny; y <= maxy; ++y) {
    for (int x = minx; x <= maxx; ++x) {
        const math::vec2 p{x + 0.5f, y + 0.5f};                 // pixel center
        const math::vec3 bc = barycentric(s0, s1, s2, p);
        if (bc.x < -1e-5f || bc.y < -1e-5f || bc.z < -1e-5f)    // outside?
            continue;
        const float depth = bc.x*z0 + bc.y*z1 + bc.z*z2;        // interpolate depth
        if (depth >= depth_[di]) continue;                      // z-test
        depth_[di] = depth;                                     // z-write
        fb_->set_pixel(x, y, color_at(p, bc));
    }
}
```

We sample at the pixel **center** (`+0.5`) so coverage is consistent. The tiny
`-1e-5` tolerance closes hairline seams between adjacent triangles.

> **Bounding-box safety.** We clamp the box's float extents to the framebuffer
> *before* casting to `int`. A vertex with a tiny `clip.w` (right at the near plane)
> can produce an enormous screen coordinate, and casting an out-of-range float to
> `int` is undefined behavior — the same class of bug we fixed for the raycaster's
> `perp_dist` in M2. Clamp first, then cast.

---

## 5. The z-buffer: making depth work

Two triangles can cover the same pixel. The **z-buffer** (depth buffer) is a second
framebuffer-sized array holding, per pixel, the depth of the closest thing drawn so
far. The rule is two lines:

```cpp
if (depth >= depth_[i]) continue;   // something nearer is already here → skip
depth_[i] = depth;                  // otherwise record this as the new nearest
```

We initialize every cell to `+∞` (`1e30f`) at `begin()`, meaning "empty." Because the
test is **draw-order independent**, we can throw triangles at it in any order and the
nearest always wins. That's exactly what `test_rasterizer_depth` proves: a near green
triangle beats a far red one whether the red is drawn first or second.

### Why a plain barycentric blend is the *correct* depth

Here is a subtle and important point. The depth we store is **NDC z** (`z_clip/w_clip`,
rescaled to `[0,1]`). It turns out NDC z is an **affine** (flat-plane) function of the
screen `x, y` — so blending it with screen-space barycentric weights is **exact**. You
do *not* perspective-correct the depth. (Try it and z-fighting appears.) This is the
opposite of what you do for *colors* — see below.

---

## 6. Perspective-correct interpolation (for colors, not depth)

Vertex **attributes** like color do *not* vary linearly across the screen — a wall
receding into the distance has its texture bunch up near the horizon. To interpolate
them correctly you weight by `bc · (1/w)`, sum, and divide by the summed `1/w`:

```cpp
const float w0 = bc.x*s0.inv_w, w1 = bc.y*s1.inv_w, w2 = bc.z*s2.inv_w;
const float ws = w0 + w1 + w2;
color = (w0*c0 + w1*c1 + w2*c2) / ws;   // perspective-correct
```

This is the Gouraud path in `raster_triangle`. (Flat shading skips it — all three
vertex colors are equal, so the result is constant anyway.)

| Quantity | How to interpolate | Why |
|---|---|---|
| **Depth** (NDC z) | plain barycentric | NDC z is affine in screen space |
| **Color / UV / normal** | `1/w`-weighted (perspective-correct) | these are affine in *world* space, not screen |

> **The float→byte trap.** Perspective-correct color can land a hair outside `[0,255]`
> when a barycentric weight is slightly negative (within our seam tolerance).
> Converting an out-of-range float to `uint8_t` is undefined behavior, so every
> channel goes through a clamp (`to_u8`) before the cast.

---

## 7. The top-left fill rule (and why seams happen)

When two triangles share an edge, which one owns the pixels exactly *on* that edge? If
both do, you get double-draws (bad with transparency); if neither, you get a 1-pixel
crack. The professional fix is the **top-left rule**: a pixel on a shared edge belongs
to the triangle for which that edge is a "top" or "left" edge. We use a simpler tactic
— a tiny negative tolerance plus the z-buffer — which is fine for our opaque solids
(the second draw at equal depth is rejected anyway). The exercises invite you to
implement the strict rule.

---

## 8. Worked example

Rasterize the triangle `a=(10,10), b=(30,10), c=(10,30)` and test the pixel `(15,15)`:
`area = edge(a,b,c) = (30-10)(30-10) - (10-10)(10-10) = 400`. Then
`w0 = edge(b,c,(15,15))/400`, etc. All three come out positive and sum to 1 → inside.
Move the test point to `(25,25)` (just outside the hypotenuse) and one weight goes
negative → skipped. That's the inside test the whole loop rests on.

---

## 9. Pitfalls

- **Casting huge floats to int.** Always clamp screen coords to the framebuffer before
  `(int)`. (See §4.)
- **Perspective-correcting depth.** Don't. NDC z is already affine; correcting it
  causes z-fighting.
- **Forgetting to clamp color bytes.** Out-of-range float → `uint8_t` is UB and shows
  up as random bright pixels at triangle edges.
- **`>=` vs `>` in the z-test.** Use `>=` (skip on equal) so coplanar second-draws
  don't overwrite — it keeps shared edges stable.
- **Reusing a stale depth buffer.** Reset it to `+∞` every frame in `begin()`, or last
  frame's geometry haunts this one.

---

## 10. Glossary

- **Rasterization** — turning a vector triangle into the set of pixels it covers.
- **Edge function** — signed cross product telling which side of a line a point is on.
- **Barycentric coordinates** — weights `(w0,w1,w2)` summing to 1 that locate a point
  inside a triangle and drive interpolation.
- **Z-buffer / depth buffer** — per-pixel nearest-depth array enabling correct
  occlusion regardless of draw order.
- **Perspective-correct interpolation** — `1/w`-weighted blending for attributes that
  are linear in world space (color, UV), not screen space.
- **Top-left rule** — tie-break deciding which triangle owns a shared-edge pixel.

## 11. Exercises

1. **Watch the z-buffer work.** In `Scene3D`, move the cube and sphere so they overlap
   on screen; confirm the nearer one wins, then disable the z-test (`continue` removed)
   and watch draw order take over incorrectly.
2. **Prove depth must be linear.** Perspective-correct the depth (divide by `ws` like
   the color) and look for z-fighting on the floor where it meets the cube.
3. **Implement the top-left rule.** Replace the `-1e-5` tolerance with the real rule
   and verify shared edges have no cracks and no doubles.
4. **Count covered pixels.** Add a counter in the inner loop and print how many pixels
   the sphere covers as you zoom — relate it to its on-screen area.
