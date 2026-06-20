# Chapter 04 — The Math Library

> **Goal of this chapter.** Build, from scratch, the vector and matrix math that
> underpins *everything* geometric — 2D drawing now, and the full 3D transform
> pipeline at M3. We'll understand *why* each piece exists (not just its formula),
> nail down the conventions that prevent the classic matrix bugs, and prove it all
> correct with a tiny test suite that `ctest` runs.

---

## 1. Why a game needs vectors and matrices

Two ideas carry an astonishing amount of a game:

- A **vector** is "an arrow with components": a position `(x, y, z)`, a velocity, a
  direction to the light, a surface normal, an RGB color. Add them, scale them,
  measure their length, find the angle between them.
- A **matrix** is "a recipe that transforms vectors": move, rotate, scale, or
  project them. Crucially, transforms **compose**: "rotate then move then view
  through a camera then project to the screen" becomes a single matrix you build
  once and apply to thousands of points.

Master these two and the 3D pipeline at M3 stops being magic — it's just "multiply
each vertex by the right matrix."

We **hand-write** them (the project's whole point), and it's genuinely a good idea
here: the code is small, we control every convention, and operator overloading lets
it read like real math.

---

## 2. Vectors

We define `vec2`, `vec3`, `vec4` as plain structs of floats, with operators:

```cpp
vec3 a{1,2,3}, b{4,5,6};
vec3 c = a + b;          // {5,7,9}
vec3 d = a * 2.0f;       // {2,4,6}   (and 2.0f * a also works)
float L = length(a);     // sqrt(1+4+9)
vec3 u = normalize(a);   // same direction, length 1
```

Without operator overloading this would be `vec3_add(a, vec3_scale(b, 2))` — noise
that hides the geometry. With it, the M3 vertex math will read like a textbook.

### The dot product — the most useful number in graphics

`dot(a, b) = a.x*b.x + a.y*b.y + a.z*b.z`. Geometrically `dot(a,b) =
|a||b|cos θ`, where θ is the angle between them. That one identity powers a lot:

- **Length:** `length(a) = sqrt(dot(a, a))`.
- **Angle / "how aligned":** for unit vectors, `dot` is directly `cos θ` — `+1`
  same direction, `0` perpendicular, `-1` opposite.
- **Lighting (M3):** surface brightness ≈ `max(0, dot(normal, light_dir))`.

### The cross product — perpendicular and orientation

`cross(a, b)` returns a vector **perpendicular** to both, following the
right-hand rule. We use it to build a camera's axes (`look_at`), to get a
triangle's facing direction for **backface culling** (M3), and its length equals
the area of the parallelogram they span. Quick sanity check baked into the tests:
`cross(x̂, ŷ) = ẑ`, i.e. `cross({1,0,0},{0,1,0}) = {0,0,1}`.

### Pitfall: normalizing the zero vector

`normalize` divides by length; a zero vector has length 0. We guard it (return zero
unchanged) so a stray zero direction can't produce `NaN`s that silently poison
everything downstream.

---

## 3. Matrices and homogeneous coordinates

### Why 4×4 for 3D?

A 3×3 matrix can rotate and scale a 3D vector, but it **cannot translate** it
(moving the origin isn't a linear operation). The fix is a classic trick:
**homogeneous coordinates** — represent a 3D point as a 4D vector with `w = 1`.
Now a 4×4 matrix's last column adds translation, and *everything* (rotate, scale,
move, even perspective) becomes a single matrix multiply.

The `w` component also distinguishes two things that transform differently:

- a **point** has `w = 1` → translation applies (`transform_point`),
- a **direction** has `w = 0` → translation is ignored (`transform_dir`).

That's why a normal or a velocity must be transformed as a direction, not a point —
moving a *direction* makes no sense, and the `w=0` makes the math enforce it.

### The column-major convention (read this once)

A 4×4 matrix is 16 floats, but *which* float is row 2, column 3? We choose
**column-major** storage to match OpenGL (smoothing the path to GL ES at M3/M5):

```
   element (row r, col c)  lives at  m[c*4 + r]

   columns →   c0   c1   c2   c3
   row0      [ m0   m4   m8   m12 ]
   row1      [ m1   m5   m9   m13 ]
   row2      [ m2   m6   m10  m14 ]
   row3      [ m3   m7   m11  m15 ]
```

To stay sane we **never** index `m[]` directly in logic — we use `at(r, c)`. The
translation lives in the last column: `at(0,3), at(1,3), at(2,3)`.

### Multiplying: order matters, right-to-left

- **Matrix × vector** (`M * v`) transforms a column vector: `result_r = Σ_c
  M(r,c)·v_c`.
- **Matrix × matrix** (`A * B`) composes two transforms into one.

The order is the #1 source of "why is my object in the wrong place" bugs. Because
we use column vectors and `M * v`, transforms apply **right to left**:

```
   (T * S) * v   ==   T * (S * v)      // SCALE first, THEN translate
```

The test `test_compose_order` pins this down: `(T*S)` scales `(1,0,0)→(2,0,0)` then
translates `→(3,0,0)`, whereas `(S*T)` translates first then scales `→(4,0,0)`.
Different results, by design — read a transform chain right-to-left.

---

## 4. The transform builders

Each builder returns a matrix; you compose them with `*`.

- **`mat4_identity`** — does nothing (the "1" of matrices).
- **`mat4_translate(t)`** — moves by `t` (last column).
- **`mat4_scale(s)`** — scales each axis (diagonal).
- **`mat4_rotate_x/y/z(angle)`** — rotate about a single axis. The 2×2 rotation
  `[[c,-s],[s,c]]` sits in the right plane; e.g. `rotate_z(90°)` sends `x̂→ŷ`.
- **`mat4_rotate(axis, angle)`** — rotate about any axis (Rodrigues' formula);
  about `ẑ` it equals `rotate_z`, which the tests verify.

> **Pitfall: degrees vs radians.** All trig here is in **radians**. Passing `90`
> (degrees) where radians are expected rotates by ~5156° worth of nonsense. Always
> wrap literals: `mat4_rotate_z(radians(90))`.

### The full pipeline (preview of M3)

In M3 a vertex travels through a chain of these:

```
   model        view            projection        viewport
   (place in →  (into camera →  (apply           (NDC → pixels
    the world)   space)          perspective)      on screen)

   pixel = viewport * projection * view * model * vertex
```

We build the later stages now so they're tested and ready.

### `look_at` — building the camera

`mat4_look_at(eye, center, up)` produces the **view matrix** that moves the world
*in front of* a camera at `eye` looking at `center`. It works by constructing the
camera's three axes — forward `f = normalize(center - eye)`, right `s =
normalize(cross(f, up))`, true up `u = cross(s, f)` — and using them to re-express
every world point in camera space. The test checks the intuitive result: a camera
at `(0,0,5)` looking at the origin maps the origin to `(0,0,-5)` — five units
straight ahead down `-Z`.

### `perspective` — making far things smaller

`mat4_perspective(fovy, aspect, near, far)` is what gives depth. The key is that it
puts a value into the output `w` proportional to distance; the later **perspective
divide** (`x/w, y/w, z/w` in `transform_point`) then shrinks distant things — that
division *is* foreshortening. It also remaps depth into the clip range so the near
plane → `-1` and the far plane → `+1` (verified by `test_perspective_depth`),
which the z-buffer will use at M3.

- **`mat4_ortho`** — projection with *no* foreshortening (parallel lines stay
  parallel); we'll use it for 2D/UI and isometric (M4).
- **`mat4_viewport`** — the final stretch from NDC `[-1,+1]` to actual pixel
  coordinates on the framebuffer.

---

## 5. Testing it (and why like this)

The math is exactly the kind of code that's *easy to get subtly wrong* and *easy to
test*, so we test it. `tests/test_math.cpp` uses no framework — just:

```cpp
#define CHECK(cond) /* prints FAIL file:line and counts failures */
static bool approx(float a, float b, float eps=1e-4f); // float-safe compare
int main() { ...tests...; return g_failures; }          // 0 = pass for CTest
```

Two deliberate choices: (1) **no dependency** keeps the test build trivial and
fast (it doesn't even link SDL); (2) **`approx`, never `==`** — floating-point
results are almost never bit-exact, so comparing with a tolerance is mandatory
(comparing floats with `==` is its own classic bug). CMake registers it:

```cmake
enable_testing()
add_executable(test_math tests/test_math.cpp)
target_link_libraries(test_math PRIVATE engine_flags)
add_test(NAME math COMMAND test_math)
```

Run it:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected:

```
1/1 Test #1: math .............................   Passed
100% tests passed, 0 tests failed out of 1
```

---

## 6. Worked example (do this on paper, then check in code)

Build a model matrix that scales a point by 2, then moves it +10 in x, and apply
it to `(1, 0, 0)`:

```
M = translate(10,0,0) * scale(2,2,2)        // right-to-left: scale first
M * (1,0,0,1):
   scale:      (1,0,0) -> (2,0,0)
   translate:  (2,0,0) -> (12,0,0)
result = (12, 0, 0)
```

Now flip the order (`scale * translate`) and predict the result before running it.
(Answer: `(22,0,0)` — translate to `(11,0,0)`, then scale ×2.)

---

## 7. Glossary

- **Homogeneous coordinate** — the extra `w`; `w=1` point, `w=0` direction.
- **Column-major** — storage where `at(r,c) = m[c*4+r]` (OpenGL convention).
- **Dot / cross product** — alignment scalar / perpendicular vector.
- **View / projection / viewport matrix** — camera space / perspective / pixels.
- **Perspective divide** — dividing by `w` to get foreshortening.
- **NDC** — normalized device coordinates, the `[-1,+1]` cube after projection.

---

## 8. Exercises

1. **Angle from dot.** Compute `dot(normalize({1,1,0}), normalize({1,0,0}))` by
   hand; what angle does that cosine correspond to? *(Hint: 45°.)*
2. **Predict, then verify.** Add a `CHECK` for `transform_point(mat4_rotate_z(
   radians(180)), {1,0,0})`. Predict the result first. *(Hint: `(-1,0,0)`.)*
3. **Break a convention on purpose.** Temporarily change `radians(90)` to `90` in
   one rotation test and watch it fail — that's the degrees/radians trap caught by
   a test instead of by a confusing render later.
4. **Normal vs point.** Translate the point `{1,0,0}` and the direction `{1,0,0}`
   by `translate(5,0,0)`. Why does only one move? *(Hint: look at `w`.)*

---

## 9. What's next

We can now describe geometry and move it around. **Chapter 05** finally draws:
we build the **2D software renderer** — clearing, plotting clipped pixels, lines
(Bresenham), filled rectangles, alpha-blended sprites, and bitmap text — all
written straight into the framebuffer, by us.
