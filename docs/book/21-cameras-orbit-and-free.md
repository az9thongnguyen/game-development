# Chapter 21 — Cameras: Orbit & Free/Fly

> **Where we are.** The pipeline (Chapter 18) needs a **view** matrix and a
> **projection** matrix. A *camera* is simply the thing that produces those two
> matrices from human-friendly state ("I'm orbiting the model at this angle" / "I'm
> standing here looking that way"). This chapter builds the two cameras the project
> needs: an **orbit camera** for inspecting objects and a **free/fly camera** for
> walking through a scene.

---

## 1. A camera is two matrices

Every camera, however it's controlled, must answer two questions for the renderer:

```cpp
math::mat4 view();              // where am I and which way do I look?
math::mat4 proj(float aspect);  // how do I project (FOV, near/far)?
```

`proj` is the same for both cameras — a `mat4_perspective(fovy, aspect, near, far)`.
The interesting part is `view`, and the elegant fact is that **`view` is the inverse of
the camera's pose**: instead of moving the camera into the world, we move the world in
front of a camera fixed at the origin. `mat4_look_at(eye, target, up)` computes that
inverse for us, so each camera's whole job is to produce a sensible `eye/target/up`.

> **Design choice: cameras are pure math.** Our cameras hold state and expose *verbs*
> (`orbit`, `zoom`, `look`, `move`) — they never read the keyboard themselves. The
> *scene* decides which input calls which verb. This keeps cameras reusable (a tool, a
> cutscene, and a game can drive the same camera differently) and **unit-testable** with
> no window. `test_cameras` exercises them directly.

---

## 2. The orbit camera

An orbit camera circles a fixed **target** at some **distance**, parameterized by two
angles: **yaw** (around the vertical axis) and **pitch** (elevation). This is the
camera you want for "show me this model from all sides."

The eye position is spherical-to-Cartesian around the target:

```cpp
math::vec3 OrbitCamera::eye() const {
    const float cp = std::cos(pitch);
    const math::vec3 dir{cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw)};
    return target + dir * distance;
}
math::mat4 OrbitCamera::view() const { return math::mat4_look_at(eye(), target, {0,1,0}); }
```

At `yaw = 0, pitch = 0` the offset is `(0, 0, 1)`, so the eye sits on `+Z` looking back
toward the target down `−Z` — matching the engine's convention. Swing `yaw` by 90° and
the eye moves onto `+X` (that's a `test_cameras` assertion).

The two verbs:

```cpp
void orbit(float dyaw, float dpitch) {              // drag / arrow keys
    yaw += dyaw;
    pitch = math::clampf(pitch + dpitch, -kPitchLimit, kPitchLimit);
}
void zoom(float factor) {                           // W/S keys
    distance = math::clampf(distance * factor, min_distance, max_distance);
}
```

**Pitch is clamped to ±89°.** Letting it reach exactly ±90° would line the view
direction up with the `up` vector, making `look_at`'s cross products collapse (the
infamous gimbal flip). One clamp prevents a whole class of "the view suddenly spun"
bugs.

---

## 3. The free/fly camera

A fly camera is a first-person eye: a **position** plus a **yaw/pitch** look direction.
It's the camera for exploring — walk forward, strafe, look around.

The look direction is built from the two angles, with `yaw = 0` looking down `−Z`:

```cpp
math::vec3 FlyCamera::forward() const {
    const float cp = std::cos(pitch);
    return math::normalize({ -std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp });
}
math::vec3 FlyCamera::right() const { return math::normalize(math::cross(forward(), {0,1,0})); }
math::mat4 FlyCamera::view() const { return math::mat4_look_at(pos, pos + forward(), {0,1,0}); }
```

`target = pos + forward` — the camera always looks one unit ahead of itself. Movement
is along the **local** axes so "forward" means "where I'm facing," not "world −Z":

```cpp
void move(float fwd, float strafe, float up) {
    pos = pos + forward() * fwd + right() * strafe + math::vec3{0,1,0} * up;
}
void look(float dyaw, float dpitch) {               // same ±89° pitch clamp
    yaw += dyaw;
    pitch = math::clampf(pitch + dpitch, -kPitchLimit, kPitchLimit);
}
```

In `Scene3D`, the same WASD/arrow keys mean *zoom/orbit* for the orbit camera and
*move/look* for the fly camera — the scene checks `use_fly_` and routes input to the
right verbs. Right-click toggles between them.

---

## 4. Worked example: where does the orbit eye sit?

With `target = (0,0,0)`, `distance = 5`, `yaw = pitch = 0`:
`dir = (cos0·sin0, sin0, cos0·cos0) = (0, 0, 1)`, so `eye = (0, 0, 5)`. The view matrix
then maps the target to view-space `(0, 0, −5)` — directly in front, 5 units down `−Z`.
`test_cameras` asserts both of these, then orbits 90° in yaw and asserts the eye has
swung to `≈ (5, 0, 0)`.

---

## 5. Pitfalls

- **Gimbal at the poles.** Always clamp pitch short of ±90°. We use ±89°.
- **Forgetting aspect ratio.** `proj` takes `aspect = width/height`; pass the wrong one
  and circles render as ellipses. The scene computes it fresh each frame from the
  framebuffer size.
- **Strafe near straight-up.** When `forward` nears the world up vector, `right`
  (their cross product) shrinks toward zero. Our `normalize` guards the divide, so
  strafing just weakens gracefully rather than exploding — but it's why the pitch clamp
  matters here too.
- **Mutating `up` over time.** Keep a fixed world `up`; deriving it from the previous
  frame accumulates roll drift.

---

## 6. Glossary

- **View matrix** — transforms world → camera space; the inverse of the camera's pose.
- **Projection matrix** — applies perspective (FOV, near/far).
- **Orbit camera** — circles a target by yaw/pitch at a distance; for inspection.
- **Free/fly camera** — first-person position + look direction; for exploration.
- **Yaw / pitch** — rotation around the vertical / horizontal axis.
- **Gimbal lock / flip** — degeneracy when the look direction aligns with `up`.

## 7. Exercises

1. **Frame the scene.** Add a verb `OrbitCamera::frame(center, radius)` that sets
   `target` and `distance` so a sphere of that radius fills the view.
2. **Mouse-look the fly camera.** Wire left-drag to `FlyCamera::look` (it's already in
   `Scene3D`) and tune the sensitivity until it feels right.
3. **Smooth zoom.** Make `zoom` ease toward a target distance over a few frames instead
   of snapping, and feel the difference.
4. **A second orbit target.** Add a key that moves the orbit target from the cube to the
   sphere and back; watch the camera re-frame.
