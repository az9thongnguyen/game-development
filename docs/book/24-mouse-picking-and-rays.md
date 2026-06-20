# Chapter 24 — Mouse Picking & Rays

> **Where we are.** The whole 3D pipeline (Chapters 18–23) runs *forward*: world →
> screen. An editor needs the *inverse*: the user clicks a pixel, and we must figure
> out which 3D object they meant. This chapter builds that — turning a 2D mouse click
> into a **world-space ray**, then intersecting that ray with objects (to select) and
> with the ground (to drag). It's the foundation of every 3D tool's interactivity.

---

## 1. The inverse problem

Forward rendering throws away a dimension: many 3D points map to the same pixel (they
line up behind each other). So "what's under the cursor?" has no single answer — it's a
whole **ray** of points shooting into the scene. Picking is: build that ray, then find
the nearest thing it hits.

```
   eye •────────────────▶ (ray through the clicked pixel)
        \         ┌───┐
         \        │ A │   ← ray hits A first (nearest) → select A
          \   ┌───┘   │
           \  │   B   │   ← also on the ray, but farther
            \ └───────┘
```

---

## 2. From a clicked pixel to a world ray

We need the inverse of "world → screen." The textbook way is to invert the full
`Projection · View` matrix and unproject two points. But we can do it more directly —
and more teachably — straight from the **camera basis**, no matrix inverse required.

First, map the mouse pixel to normalized device coordinates (`[-1,+1]`, y up):

```cpp
const float ndc_x = 2.0f * mouse_x / W - 1.0f;
const float ndc_y = 1.0f - 2.0f * mouse_y / H;   // flip: screen y is down
```

Then build the ray from the camera's forward/right/up axes and its field of view
(`OrbitCamera::ray_through`):

```cpp
Ray OrbitCamera::ray_through(float ndc_x, float ndc_y, float aspect) const {
    const float t = std::tan(fovy * 0.5f);          // half-FOV "spread"
    const math::vec3 dir = math::normalize(forward()
        + right() * (ndc_x * t * aspect)            // pan across the view
        + up()    * (ndc_y * t));                   // and up/down
    return {eye(), dir};                            // origin at the camera
}
```

At the screen center (`ndc = 0,0`) the ray is exactly `forward()` — it points at the
orbit target. Toward the edges, the `right`/`up` terms tilt it out by the FOV. The
`aspect` factor stretches the horizontal spread so a wide window doesn't squash the ray
angles. This is literally the projection matrix run backwards, expressed in vectors.

---

## 3. Ray vs sphere — selecting an object

We give each object a **bounding sphere** (center = position, radius = a generous cover
of its geometry, scaled with the object). Picking tests the ray against each sphere; the
nearest hit wins. The intersection is a quadratic (`pick.hpp`):

```cpp
float ray_sphere(vec3 o, vec3 d, vec3 c, float r) {   // d is unit
    const vec3 oc = o - c;
    const float b = dot(oc, d);
    const float cc = dot(oc, oc) - r * r;
    const float disc = b * b - cc;                    // discriminant (a == 1)
    if (disc < 0.0f) return -1.0f;                    // misses the sphere
    const float s = std::sqrt(disc);
    float t = -b - s;                                 // near intersection
    if (t < 0.0f) t = -b + s;                         // origin inside → far one
    return t >= 0.0f ? t : -1.0f;
}
```

Because `d` is normalized, the quadratic's `a` term is 1, so it collapses to the tidy
form above. The two roots are the entry and exit distances; we want the nearer positive
one (and fall back to the far root if the camera is *inside* the sphere). `Editor::pick`
just loops every object and keeps the smallest `t`:

```cpp
int Editor::pick(vec3 ro, vec3 rd) const {
    int best = -1; float best_t = 1e30f;
    for (int i = 0; i < (int)objects_.size(); ++i) {
        const float t = pick::ray_sphere(ro, rd, objects_[i].pos, objects_[i].bound_radius());
        if (t >= 0.0f && t < best_t) { best_t = t; best = i; }
    }
    return best;   // -1 = clicked empty space
}
```

**Why bounding spheres** and not exact triangle tests? They're one cheap formula,
rotation-invariant (a sphere looks the same spun any way), and plenty accurate for
selecting whole objects. Per-triangle picking is the upgrade when you need to click a
*face*.

---

## 4. Ray vs ground plane — dragging an object

To move a selected object by dragging, we intersect the mouse ray with a horizontal
plane at the object's current height and place the object where it lands:

```cpp
bool ray_plane_y(vec3 o, vec3 d, float py, vec3& out) {
    if (std::fabs(d.y) < 1e-6f) return false;   // ray parallel to the plane
    const float t = (py - o.y) / d.y;
    if (t < 0.0f) return false;                 // plane is behind the ray
    out = o + d * t;
    return true;
}
```

Each frame of a drag, `EditorScene` re-casts the ray and snaps the object's `x,z` to the
hit point — so the object follows the cursor across the ground exactly, at any camera
angle. (Its `y` is left to the `Q`/`E` keys.)

---

## 5. Worked example

Camera at `eye = (0,0,5)`, looking at the origin. Click dead center → `ndc = (0,0)` →
`dir = forward() = (0,0,-1)`. Test against a unit sphere at the origin:
`oc = (0,0,5)`, `b = dot(oc,d) = -5`, `cc = 25 - 1 = 24`, `disc = 25 - 24 = 1`,
`s = 1`, `t = 5 - 1 = 4`. The ray hits 4 units away — exactly the front of the sphere.
Click far to the side and `disc < 0` → miss → `pick` returns `-1` → deselect. These are
`test_viz3d`'s `ray_sphere` and `pick` assertions.

---

## 6. Pitfalls

- **Forgetting the y-flip** in the pixel→NDC step picks the vertically-mirrored object.
- **A non-normalized ray direction** breaks the `a == 1` assumption in `ray_sphere`
  (and skews `t` as a distance). Always normalize `dir`.
- **Bounding spheres too tight** make objects hard to click; **too loose** makes
  selection ambiguous when objects are close. Tune `bound_radius` per shape.
- **Ignoring the near/inside root.** Clicking while the camera is inside an object
  should still select it — hence the far-root fallback.
- **Plane drag with a near-horizontal ray** (`d.y ≈ 0`) sends the object to infinity;
  the `1e-6` guard rejects it.

---

## 7. Glossary

- **Picking** — determining which object a screen click refers to.
- **Pick ray** — the world-space ray from the camera through the clicked pixel.
- **Unproject** — mapping screen/NDC coordinates back into world space.
- **Bounding sphere** — a sphere enclosing an object, used for cheap intersection.
- **Discriminant** — the `b² − c` term whose sign says whether a ray meets a sphere.

## 8. Exercises

1. **Tighter picks.** Replace bounding-sphere picking with ray-vs-AABB (axis-aligned box)
   for the cube and compare how selection "feels."
2. **Hover highlight.** Run `pick` every frame (not just on click) and outline the
   hovered object in a dim color, the selected one in bright yellow.
3. **Per-face pick.** Intersect the ray with the actual triangles of the picked mesh
   (Möller–Trumbore) and report which face was clicked.
4. **Snap to grid.** Round the `ray_plane_y` hit point to the nearest grid line so
   dragged objects snap into place.
