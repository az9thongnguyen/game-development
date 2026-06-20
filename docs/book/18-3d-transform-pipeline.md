# Chapter 18 — The 3D Transform Pipeline

> **Where we are.** M0 gave us a framebuffer and a math library. M1 and M2 drew
> into that framebuffer in 2D (chess) and with a clever 2.5D trick (the raycaster).
> Now we build the real thing: a **3D pipeline** that takes triangles floating in
> world space and figures out exactly which pixels they cover. This chapter is the
> *map of spaces* a vertex travels through. Chapter 19 fills the triangles in.

---

## 1. The problem

A 3D model is a list of points (vertices) with coordinates like `(1.0, -0.5, 2.0)`
in *its own* space. The screen is a grid of pixels like `(640, 360)`. The whole job
of the transform pipeline is to answer: **given a vertex in model space and a camera,
what pixel (and what depth) does it land on?**

We solve it the way every GPU does — by pushing the vertex through a chain of
coordinate systems, each one a matrix multiply, until it lands on the screen:

```
   model space ──M──▶ world ──V──▶ view/eye ──P──▶ clip ──÷w──▶ NDC ──viewport──▶ screen
     (the mesh)      (placed)     (camera at    (4D, ready    (cube      (pixels + depth)
                                   the origin)   to divide)   [-1,+1]³)
```

Three matrices (`M`, `V`, `P`), one **perspective divide** (`÷w`), and one viewport
map. Let's walk each arrow.

---

## 2. The spaces, one arrow at a time

### Model → World: the *model matrix* `M`
Every mesh is authored around its own origin (a cube centered on `(0,0,0)`). To
place it in the world — move it, spin it, scale it — we multiply each vertex by a
**model matrix** built from translate × rotate × scale (TRS):

```cpp
math::mat4 model = math::mat4_translate({2, 0, 0})
                 * math::mat4_rotate_y(angle)
                 * math::mat4_scale({1, 1, 1});
```

Order matters: read it **right to left** as "scale, then rotate, then translate."
Scaling after translating would move the object *and* stretch the distance from the
origin — almost never what you want.

### World → View: the *view matrix* `V`
There is no "move the camera" in rendering — instead we move the **whole world** so
the camera sits at the origin looking down `-Z`. That inverse-of-the-camera-pose is
the view matrix, and `mat4_look_at` builds it from three intuitive inputs:

```cpp
math::mat4 view = math::mat4_look_at(eye, target, up);
```

`eye` is where the camera is, `target` is what it looks at, `up` is which way is up.
After `V`, a point that was *in front of the camera* has a **negative Z** (because we
look down `-Z`). Chapter 21 builds the two camera types that produce `eye/target/up`.

### View → Clip: the *projection matrix* `P`
This is the magic step — it encodes perspective (far things look smaller). Our
`mat4_perspective(fovy, aspect, near, far)` produces a matrix whose key trick is in
the bottom row:

```cpp
r.at(3, 2) = -1.0f;   // w_out = -z_eye
```

After multiplying, the 4th component `w` of the result is **the eye-space distance**.
We haven't divided by it yet — that's the next step — but the matrix has *set up* the
divide. The output of `P` is **clip space**, a 4D homogeneous coordinate `(x, y, z, w)`.

### Clip → NDC: the *perspective divide* `÷w`
Dividing `(x, y, z)` by `w` is what actually makes distant things shrink:

```
ndc = (x/w, y/w, z/w)
```

A vertex twice as far has roughly twice the `w`, so its `x/w` is half as large — it
moves toward the screen center. After the divide we are in **Normalized Device
Coordinates**: a tidy cube where everything visible lies in `[-1, +1]` on all three
axes. `-1 ≤ z ≤ +1` means "between the near and far planes."

### NDC → Screen: the *viewport* map
Finally we stretch the `[-1, +1]` square to the pixel rectangle and **flip Y**
(NDC `+y` is up; our framebuffer `+y` is down). We also squash `z` into `[0, 1]` for
the depth buffer. This is `to_screen` in `engine/pipeline.hpp`:

```cpp
inline Screen to_screen(const math::vec4& clip, int W, int H) {
    Screen s;
    s.inv_w = 1.0f / clip.w;
    const float ndc_x = clip.x * s.inv_w;
    const float ndc_y = clip.y * s.inv_w;
    const float ndc_z = clip.z * s.inv_w;
    s.x = (ndc_x * 0.5f + 0.5f) * W;
    s.y = (1.0f - (ndc_y * 0.5f + 0.5f)) * H;   // ← the Y flip
    s.depth = ndc_z * 0.5f + 0.5f;              // [0,1], near=0 far=1
    return s;
}
```

We keep `inv_w` (= `1/w`) around because Chapter 19 needs it for perspective-correct
interpolation.

---

## 3. Why four dimensions? Homogeneous coordinates

A 3×3 matrix can rotate and scale, but it **cannot translate** (moving the origin is
not a linear operation). The fix is an old trick: add a 4th coordinate `w` and work in
4D. A *point* gets `w = 1`, a *direction* gets `w = 0`:

```cpp
math::vec4 point     {x, y, z, 1.0f};   // affected by translation
math::vec4 direction {x, y, z, 0.0f};   // immune to translation (e.g. a normal)
```

Now a single 4×4 matrix can rotate, scale, **and** translate — and, crucially, encode
*perspective* in that bottom row. The price is one division at the end (`÷w`). Our
`math::transform_point` does the point version with the divide built in; the pipeline
does it explicitly so it can keep `inv_w`.

> **The `w + z ≥ 0` test.** A point is in front of the near plane exactly when its
> NDC `z ≥ -1`, i.e. `z_clip / w_clip ≥ -1`, i.e. `z_clip + w_clip ≥ 0` (for `w > 0`).
> That single inequality is the near-plane test we clip against in Chapter 22 — and
> it's why we clip *before* dividing by `w` (after the divide, a behind-camera vertex
> with `w < 0` has already corrupted its coordinates).

---

## 4. Worked example: project a point by hand

Camera at `eye = (0, 0, 5)`, looking at the origin, `up = +Y`. Project the world point
`P = (0, 0, 0)` onto a 400×200 screen with a 60° vertical FOV.

1. **View.** `look_at` puts the camera at the origin looking `-Z`. `P` is 5 units in
   front → view-space `P_v = (0, 0, -5)`.
2. **Projection.** With `f = 1/tan(30°) ≈ 1.732`, aspect `= 2`:
   - `x_clip = (f/aspect)·0 = 0`
   - `y_clip = f·0 = 0`
   - `w_clip = -z_eye = 5`
3. **Divide.** `ndc = (0/5, 0/5, …) = (0, 0, …)`.
4. **Viewport.** `x = (0·0.5+0.5)·400 = 200`, `y = (1-(0·0.5+0.5))·200 = 100`.

The world origin lands dead center — exactly what `test_mvp_projection` asserts. Move
`P` to `(1, 0, 0)` and `x_clip` becomes positive, so the screen `x` moves right of
center. That's the entire pipeline in four lines of arithmetic.

---

## 5. Conventions (memorize these — they prevent 90% of 3D bugs)

| Convention | Our choice | Set in |
|---|---|---|
| Matrix storage | **column-major** (`m[c*4+r]`) | `math.hpp` |
| Vector form | column vectors, transform as `M*v` | `math.hpp` |
| Handedness | right-handed, camera looks down **−Z** | `mat4_look_at` |
| Clip depth | near → −1, far → +1 | `mat4_perspective` |
| Screen Y | `+y` is **down**; flipped in `to_screen` | `pipeline.hpp` |
| Depth buffer | `[0,1]`, **smaller = nearer** | `to_screen` |

---

## 6. Pitfalls

- **Row- vs column-major confusion.** If your transforms come out transposed or your
  multiply order feels reversed, you've mixed conventions. We are column-major,
  `M*v`, multiply right-to-left. Stay consistent and never index `m[]` raw — use
  `at(r,c)`.
- **Forgetting the perspective divide.** Clip space looks almost like screen space,
  but skipping `÷w` removes all perspective (objects don't shrink with distance).
- **Dividing when `w ≤ 0`.** A vertex on or behind the camera has `w ≤ 0`; dividing
  flips its sign and smears the triangle across the screen. We *clip the near plane
  first* (Chapter 22) so this never happens.
- **Forgetting the Y flip.** Skip it and your scene renders upside down — a classic
  first-3D-frame bug.

---

## 7. Glossary

- **Homogeneous coordinate** — a 4D `(x,y,z,w)` representation that lets one matrix do
  rotation, scale, translation, and perspective.
- **Clip space** — the 4D space right after the projection matrix, before `÷w`.
- **NDC (Normalized Device Coordinates)** — the `[-1,+1]³` cube after the divide.
- **Perspective divide** — dividing `(x,y,z)` by `w`; what creates foreshortening.
- **Viewport transform** — the map from NDC to pixel coordinates (+ Y flip + depth).
- **MVP** — the combined `Projection · View · Model` matrix.

## 8. Exercises

1. **Trace a vertex.** Pick `eye = (0,0,3)` and project `(1,1,0)` by hand to a 100×100
   screen at 90° FOV. Check your answer by adding a `printf` in `to_screen`.
2. **Break the divide.** Temporarily skip `÷w` (use `clip.x` instead of `clip.x/w`) in
   a copy of `to_screen` and observe the scene flatten — perspective disappears.
3. **Flip the flip.** Remove the `1.0f -` from the Y line and watch the scene turn
   upside down. Put it back.
4. **Why right-to-left?** Build a model matrix as `scale * translate` instead of
   `translate * scale` and explain (or observe) why the cube flies off-center.
