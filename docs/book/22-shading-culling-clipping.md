# Chapter 22 — Shading, Culling & Clipping

> **Where we are.** We can transform (18), fill (19), build (20), and aim (21). This
> chapter adds the three things that turn a flat-colored blob into a believable solid:
> **backface culling** (don't draw what faces away), **shading** (light it), and
> **near-plane clipping** (handle geometry that crosses behind the camera). Together
> they're the difference between "triangles on screen" and "a 3D object."

---

## 1. Backface culling

A closed solid shows you only its front-facing triangles; the back ones are hidden
inside. Drawing them is wasted work — and worse, with transparency or thin shells it
looks wrong. **Backface culling** skips them.

The test reuses the edge function from Chapter 19: the **signed area** of the triangle
*in screen space*. Recall its sign encodes winding. After our Y-flip (Chapter 18), a
front-facing (CCW-in-world) triangle ends up with a **negative** signed area:

```cpp
const float area = signed_area({s0.x,s0.y}, {s1.x,s1.y}, {s2.x,s2.y});
if (area == 0.0f) return;            // degenerate: no pixels
if (cull_ && area > 0.0f) return;    // back face: skip
```

That's the whole feature: one cross product per triangle. Turn it off (`SPACE` in the
demo) and you'll see the insides of shapes show through — useful for open meshes like a
single plane viewed from below, which is *why* it's a toggle and not always-on.

> **Why the sign flips.** In world space we wind front faces CCW. The viewport Y-flip
> mirrors the triangle vertically, which reverses its apparent winding on screen — so
> "front" becomes negative signed area. If your culling hides exactly the wrong faces,
> you've got a sign or a Y-flip inconsistency; flip the comparison or the flip, not
> both.

---

## 2. Lighting: the Lambert model

We use the simplest believable lighting: a single **directional light** (like the sun —
parallel rays, one direction) and **Lambert's cosine law** (a surface is brightest when
it faces the light head-on, dimming with the cosine of the angle between its normal and
the light).

```cpp
float lambert(math::vec3 n, const Light& light) {
    const float d = math::dot(n, -light.dir);          // how much n faces the source
    const float diffuse = d > 0.0f ? d : 0.0f;          // no negative light
    return light.ambient + (1.0f - light.ambient) * diffuse;
}
```

`light.dir` is the direction the light *travels*, so a surface is lit by how much its
normal faces `−dir`. The `ambient` term (0.25) is a floor so faces turned away aren't
pure black — a cheap stand-in for bounced light. The result multiplies the surface's
base color (`shade(color, lambert)`), clamped per channel.

---

## 3. Flat vs Gouraud shading

Two ways to apply that lighting across a triangle:

- **Flat** — compute lighting **once per triangle** from the *face* normal; the whole
  triangle is one color. Crisp facets. Cheap. Great for hard-edged shapes.
- **Gouraud** — compute lighting **per vertex** from the *vertex* normals, then
  interpolate the resulting colors (perspective-correctly, Chapter 19) across the
  triangle. Smooth gradients. Great for curved shapes.

```cpp
if (mode == Mode::SolidFlat) {
    math::vec3 fn = normalize(cross(wb - wa, wc - wa));    // face normal in world space
    gfx::Color fc = shade(a.color, lambert(fn, light));
    ca = cb = cc = fc;                                     // same color → flat
} else if (mode == Mode::SolidGouraud) {
    ca = shade(a.color, lambert(normalize(transform_dir(model, a.normal)), light));
    cb = shade(b.color, lambert(normalize(transform_dir(model, b.normal)), light));
    cc = shade(c.color, lambert(normalize(transform_dir(model, c.normal)), light));
}
```

The visual payoff is clearest on the sphere: flat shading shows obvious facets; Gouraud
melts them into a smooth ball. On the cube it's the opposite lesson — Gouraud's averaged
corner normals make it look slightly *rounded* (see Chapter 20 §3). Press `ENTER` in the
demo to cycle wireframe → flat → Gouraud and watch both.

> **Transforming normals.** We rotate normals with `transform_dir` (translation-free)
> and re-normalize. This is exactly right for rotation + uniform scale, which is all the
> demo uses. For **non-uniform** scale you'd need the *inverse-transpose* of the model
> matrix, or normals come out skewed — a worthwhile upgrade left as an exercise.

---

## 4. Near-plane clipping

Here's a problem the bounding box can't solve: a triangle with one vertex **behind the
camera**. That vertex has `w ≤ 0`, and the perspective divide (Chapter 18) flips its
sign — the triangle smears across the entire screen. The only fix is to **cut the
triangle along the near plane** *before* dividing, keeping just the part in front.

We clip against the single plane `w + z ≥ 0` (the near plane, Chapter 18 §3) with
**Sutherland-Hodgman**: walk the triangle's edges; keep inside vertices; where an edge
crosses the plane, insert the intersection point (linearly interpolating position *and*
color). The result is a polygon of 3 or 4 vertices, which we fan into 1 or 2 triangles:

```cpp
int clip_near(const ClipV in[3], ClipV out[2][3]) {
    ClipV poly[4]; int n = 0;
    for (int i = 0; i < 3; ++i) {
        const ClipV& cur = in[i]; const ClipV& nxt = in[(i+1)%3];
        const float dc = cur.clip.w + cur.clip.z;     // ≥ 0 means inside
        const float dn = nxt.clip.w + nxt.clip.z;
        if (dc >= 0.0f) poly[n++] = cur;              // keep inside vertex
        if ((dc >= 0.0f) != (dn >= 0.0f))             // edge crosses the plane
            poly[n++] = lerp_clipv(cur, nxt, dc / (dc - dn));   // insert crossing
    }
    if (n < 3) return 0;                              // fully clipped away
    out[0][0]=poly[0]; out[0][1]=poly[1]; out[0][2]=poly[2];
    if (n == 4) { out[1][0]=poly[0]; out[1][1]=poly[2]; out[1][2]=poly[3]; return 2; }
    return 1;
}
```

**Why only the near plane?** It's the one plane that *must* be clipped — it's where `w`
changes sign and the math breaks. The left/right/top/bottom/far planes are handled for
free: off-screen pixels are simply skipped by the bounding-box clamp (Chapter 19), and
too-far pixels lose the z-test. Clipping all six planes is more correct but unnecessary
for a CPU renderer at our scale. `test_clip_near` checks the three cases: all-in
(1 triangle), all-behind (0), straddling (2, every output vertex satisfying `w+z ≥ 0`).

The line renderer (`draw_lines`, used for the grid and axes) does the same clip on a
*segment*: if one endpoint is behind the near plane, slide it to the crossing point
(and interpolate its color).

---

## 5. Pitfalls

- **Culling sign.** Get the front-face sign wrong and you cull exactly the visible
  faces (object turns inside-out or vanishes). Verify with a known shape (our cube) and
  the `cull` toggle.
- **Skipping near-clip.** Without it, walking *through* an object (fly camera) makes
  geometry explode across the screen the instant a vertex passes behind you.
- **Normals after scaling.** Re-normalize transformed normals; for non-uniform scale use
  the inverse-transpose.
- **Black faces.** If lit faces go pure black, your `ambient` floor is missing or your
  normal points the wrong way (negate it or fix the winding).
- **Unclamped shaded color.** `shade`/Gouraud must clamp to `[0,255]` before the byte
  cast (UB otherwise) — see Chapter 19 §6.

---

## 6. Glossary

- **Backface culling** — skipping triangles that face away from the camera, via the
  screen-space signed-area sign.
- **Directional light** — parallel light rays from one direction (the sun model).
- **Lambert / diffuse** — brightness proportional to `max(0, n·−lightDir)`.
- **Ambient** — a constant light floor approximating bounced light.
- **Flat shading** — one lit color per triangle (face normal).
- **Gouraud shading** — per-vertex lit colors interpolated across the triangle.
- **Near-plane clipping** — cutting triangles/segments at the near plane before the
  perspective divide (Sutherland-Hodgman).

## 7. Exercises

1. **Move the sun.** Animate `light.dir` in a circle and watch the shading sweep across
   the sphere. Which shading mode reveals the motion best?
2. **Specular highlight.** Add a Phong specular term (reflect the light about the normal,
   dot with the view direction, raise to a power) and give the sphere a shiny spot.
3. **Inverse-transpose normals.** Apply a non-uniform scale to the cube and fix the
   shading by transforming normals with the inverse-transpose of the model matrix.
4. **Full frustum clip.** Extend `clip_near` to also clip left/right/top — does the
   scene look any different? Where would it matter?
