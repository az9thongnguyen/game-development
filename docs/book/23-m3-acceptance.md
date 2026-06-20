# Chapter 23 — M3 Acceptance: The 3D Core in Action

> **Where we are.** Chapters 18–22 built the pieces: the transform pipeline, the
> z-buffered rasterizer, meshes and generators, cameras, and shading/culling/clipping.
> This chapter assembles them into the **`viz3d` scene** — the milestone deliverable —
> shows how a frame is composed, maps the result back to the requirements, and points at
> what comes next (the M3.5 sandbox). This is the chapter that proves the 3D core is a
> *reusable subsystem*, not a one-off demo.

---

## 1. Run it

```sh
cmake --build build
./build/demo --3d
```

You should see a spinning orange cube and a blue sphere floating above a dark floor
that recedes into the distance, crossed by a perspective grid, with red/green/blue axes
at the origin. A HUD reports the current mode. Controls:

| Input | Orbit camera | Fly camera |
|---|---|---|
| left-drag | orbit around the scene | look around |
| W / S | zoom in / out | move forward / back |
| A / D | — | strafe left / right |
| arrows | orbit / pitch | look |
| **ENTER** | cycle shading: wireframe → flat → Gouraud | (same) |
| **SPACE** | toggle backface culling | (same) |
| **right-click** | switch to fly camera | switch to orbit camera |
| **ESC** | quit | quit |

---

## 2. How one frame is composed

`Scene3D::render` is the whole 3D core in fifteen lines — and it reads like a
recipe, which is the point:

```cpp
const math::mat4 view = use_fly_ ? fly_.view()       : orbit_.view();
const math::mat4 proj = use_fly_ ? fly_.proj(aspect) : orbit_.proj(aspect);

r3_.begin(g, gfx::rgb(22, 24, 30));   // bind framebuffer, clear color, reset depth (Ch.19)
r3_.set_camera(view, proj);           // the V and P matrices (Ch.18, 21)
r3_.set_cull(cull_);                  // backface toggle (Ch.22)

r3_.draw_mesh(floor_, floorM, Mode::SolidFlat, light_);   // solid ground
r3_.draw_lines(grid_, gridM);                             // grid over it (Ch.20 line list)
r3_.draw_lines(axes_, I);                                 // orientation gizmo

r3_.draw_mesh(cube_,   cubeM, mode_, light_);             // the two spinning solids,
r3_.draw_mesh(sphere_, sphM,  mode_, light_);             // in the selected shading mode
```

Each `draw_mesh` runs the full per-triangle pipeline: MVP transform → near-clip →
perspective divide → viewport+Y-flip → backface cull → barycentric z-buffered fill with
flat/Gouraud shading. The **draw order matters** for the line overlays: we draw the
solid floor first, then the grid/axes lines on top of it, then the solid objects (which
the z-buffer lets correctly occlude the grid behind them).

> **No per-frame allocation.** `Renderer3D` is a *member* of the scene; `begin(g, …)`
> binds this frame's framebuffer and reuses the persistent depth buffer (only resizing
> if the window size changes). The render hot path allocates nothing — meeting the
> performance rule from `requirements.md` §9.

---

## 3. What the three shading modes look like

Cycling `ENTER` is the quickest correctness check for the whole renderer:

- **Flat** — the cube has crisp, distinctly-lit faces (top brighter than sides); the
  sphere shows clear facets. Confirms face-normal lighting + the z-buffer.
- **Gouraud** — the sphere becomes perfectly smooth (per-vertex lighting interpolated
  perspective-correctly); the cube goes slightly soft/rounded at the corners — the
  averaged-normal effect from Chapter 20. Confirms perspective-correct interpolation.
- **Wireframe** — triangle edges only; you can see the cube's quad diagonals, the
  sphere's lat/long mesh, and the floor grid showing *through* the unfilled shapes.
  With culling on, back-facing edges are dropped. Confirms projection + clipping +
  culling independent of fill.

That all three render correctly from the same geometry and camera is the visual proof
that the pipeline is sound.

---

## 4. Acceptance checklist (maps to `requirements.md` §6/§8/§10)

- [x] **Software triangle rasterizer with z-buffer + perspective** — `renderer3d.cpp`;
  proven by `test_render3d` (depth ordering is draw-order independent) and the visual
  occlusion of grid by solids.
- [x] **Full transform pipeline** model→view→projection→viewport — `pipeline.hpp` +
  `math.hpp`; `test_mvp_projection`.
- [x] **Mesh + primitive generators** (cube, plane, sphere, grid) — `geometry.cpp`;
  `test_geometry`.
- [x] **Orbit + free cameras** — `camera.cpp`; `test_cameras`.
- [x] **Wireframe + solid (flat & Gouraud)** — `Renderer3D::Mode`.
- [x] **Backface culling** + **coordinate axes & grid** for orientation.
- [x] **No SDL above the platform layer** — `grep -rn SDL src/engine src/games/viz3d`
  finds only comments.
- [x] **`ctest` green** (math, render3d, chess, fps), **headless `--3d` exits 0**,
  **0 leaks**, **warning-clean**.

Per `requirements.md` §10, M3's bar is "a *reusable subsystem*, not a demo." The
renderer, geometry, and cameras live in `engine/` (the `render3d_core` library) and know
nothing about this scene — `viz3d` just *uses* them. That reuse is what M3.5 builds on.

---

## 5. The tests, and what each guards

`tests/test_render3d.cpp` (CTest target `render3d`) is fully headless — it never opens a
window:

| Test | Guards |
|---|---|
| `test_to_screen`, `test_mvp_projection` | the projection math + Y-flip + depth range |
| `test_signed_area_winding` | the backface/winding sign |
| `test_barycentric` | the inside test + interpolation weights |
| `test_clip_near` | near-plane clipping (front / behind / straddle) |
| `test_lerp_color` | clip/Gouraud color interpolation |
| `test_geometry` | generator counts + normal correctness |
| `test_rasterizer_depth` | the z-buffer (nearer wins, any draw order) |
| `test_cameras` | orbit eye/zoom/clamp + fly forward/move/look |
| `test_near_clip_raster` | clip→raster→Gouraud path is crash-free + draws |

The rasterizer tests run by driving a **heap-allocated framebuffer** through `Renderer2D`
directly — the same trick that let us unit-test pixels without SDL.

---

## 6. Known simplifications (honest scope)

These are deliberate, documented choices — not oversights:

- **Only the near plane is clipped.** Off-screen and far geometry is handled by the
  bounding-box clamp and z-test (Chapter 22 §4).
- **Lines are overlaid, not depth-tested.** The grid/axes are drawn before solids; a
  grid line technically in front of a solid won't occlude it. Fine for orientation aids.
- **8-vertex cube → rounded Gouraud.** Intentional teaching contrast (Chapter 20 §3).
- **Normals use `transform_dir`** (correct for rotation/uniform scale; non-uniform scale
  would want the inverse-transpose).

Each is a one-step upgrade if a future scene needs it — and a good exercise.

---

## 7. What's next: M3.5 `viz3d` sandbox

The `viz3d` scene is named for what it grows into. M3 proved the core renders and
rotates meshes with correct depth; **M3.5** turns it interactive: create/place/transform
multiple objects with the mouse, pick them, toggle helpers — the foundation for the
visualization tools that motivated this whole project. Because the 3D core is a clean,
tested library, M3.5 is *new scene code on top*, not engine surgery.

---

## 8. Recap of Part M3

You now have, hand-written and tested: a perspective transform pipeline, a z-buffered
triangle rasterizer with flat/Gouraud shading, mesh geometry with primitive generators,
two cameras, backface culling, and near-plane clipping — all writing into the same CPU
framebuffer as the 2D renderer, with zero SDL above the platform seam and a clear path to
the web. That is the engine's pillar.

## 9. Exercises

1. **Add a primitive to the scene.** Drop your `make_cylinder` (Chapter 20 exercise)
   into `Scene3D` beside the cube and sphere.
2. **A second light.** Add a second directional light of a different color and sum their
   contributions in `lambert`.
3. **Screenshot key.** Add a key that dumps the framebuffer to a BMP (see the offline
   `shot.cpp` approach used to verify this chapter) so you can capture poses.
4. **Toward M3.5.** Make right-drag *translate* the selected object along the ground
   plane — the first interaction of the sandbox.
