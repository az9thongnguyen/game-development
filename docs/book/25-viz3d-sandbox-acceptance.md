# Chapter 25 — The viz3d Sandbox (M3.5 Acceptance)

> **Where we are.** Chapter 23 left us with a 3D core that renders and rotates meshes.
> Chapter 24 added picking. This chapter assembles them into an **interactive editor**:
> spawn, select, move, rotate, scale, and delete 3D objects with the mouse and keyboard.
> It's the M3.5 deliverable and the foundation for real visualization tools — and it's
> mostly *new scene code on top of a tested core*, exactly as a reusable subsystem should
> allow.

---

## 1. Run it

```sh
cmake --build build
./build/demo --viz3d
```

You start with one cube. Build a little scene:

| Input | Action |
|---|---|
| **1 / 2 / 3 / 4** | spawn cube / sphere / plane / cylinder (at the camera target) |
| **left-click** | select the object under the cursor (empty space → deselect) |
| **Tab** | cycle to the next object |
| **left-drag** (on selected) | slide it along its ground plane (follows the cursor) |
| **Q / E** | move selected down / up |
| **arrows** | rotate selected (or orbit the camera if nothing is selected) |
| **− / =** | scale selected down / up |
| **Delete / Backspace** | remove selected |
| **Enter** | cycle shading: wireframe → flat → Gouraud |
| **G / X / C** | toggle grid / axes / backface culling |
| **F** | focus the camera on the selected object |
| **right-drag** orbit · **middle-drag** pan · **W / S** zoom · **Esc** quit |

The selected object wears a **yellow wireframe highlight**, and the HUD reports the
object count, selection, scale, shading mode, culling, and FPS.

---

## 2. Model vs. scene — the clean split

The sandbox is two pieces, deliberately separated:

- **`editor.hpp/.cpp` — the model.** Pure data + verbs: a list of `Object`s (shape +
  position + rotation + scale + color) and an `Editor` that can `spawn`, `remove`,
  `select`, `cycle`, and `pick`. It knows nothing about input or rendering, so it is
  **unit-tested** headlessly (`test_viz3d`).
- **`editor_scene.hpp/.cpp` — the shell.** Maps mouse/keys to those verbs, drives the
  camera, and draws. It holds no scene logic the model couldn't — it's glue.

This is the same model/view discipline that made chess and the raycaster testable:
the part with rules is pure; the part with I/O is thin.

```cpp
struct Object {
    Shape shape; math::vec3 pos; float yaw, pitch, scale; gfx::Color color;
    math::mat4 model() const;       // T · Ry · Rx · S  (Chapter 18)
    float      bound_radius() const;// world bounding sphere (Chapter 24 picking)
};
```

---

## 3. The interactive loop

`EditorScene::update` is a clear pipeline of input → verb, ordered so a fresh selection
pointer is never used across a mutation:

1. **Toggles** (Enter/G/X/C).
2. **Mutations** — spawn (1–4), delete, Tab-cycle, focus (F). These change the object
   list, so they run before anything dereferences the selection.
3. **Camera zoom** (W/S).
4. **Mouse** — on left-press, cast the pick ray (Chapter 24) and select (or deselect);
   while held, either slide the object on its ground plane or orbit; right-drag orbits,
   middle-drag pans.
5. **Keyboard transform** of the *freshly re-fetched* selection — rotate (arrows), lift
   (Q/E), scale (−/=).

Spawning at the camera **target** means new objects appear right where you're looking,
already selected and ready to place.

---

## 4. Rendering the scene

`render` composes the M3 core (Chapter 23) plus two editor touches:

```cpp
r3_.begin(g, sky);  r3_.set_camera(cam_.view(), cam_.proj(aspect));  r3_.set_cull(cull_);
if (show_grid_) r3_.draw_lines(grid_, I);
if (show_axes_) r3_.draw_lines(axes_, I);
for (const Object& o : editor_.objects()) {
    geo::Mesh& m = mesh_for(o.shape);
    for (auto& v : m.vertices) v.color = o.color;   // recolor the shared template
    r3_.draw_mesh(m, o.model(), mode_, light_);
}
if (const Object* sel = editor_.selected())
    r3_.draw_wire(mesh_for(sel->shape), sel->model(), gfx::colors::yellow);  // highlight
```

Two ideas worth noting:

- **Shared mesh templates.** There is one cube mesh, one sphere, etc. Every object of
  that shape draws the *same* mesh with its own `model()` matrix — the indexed-reuse idea
  from Chapter 20, now at the object level. We briefly recolor the template to the
  object's color before each draw.
- **The highlight.** `draw_wire` overlays the selected mesh's edges in a fixed yellow,
  independent of shading mode — instant visual feedback for "this is selected."

---

## 5. A robustness fix worth understanding

The editor lets you drag an object far out and fly the camera right up to it. That makes
a previously-unreachable case reachable: a triangle/line vertex sitting *just* in front
of the near plane but far off to the side projects to a **huge** screen coordinate
(small `w`, large `x/w`). Casting that to `int` is undefined behavior, and a Bresenham
line to it would loop hundreds of thousands of times — a visible stall.

The fix (Chapter 19's lesson, applied to lines) is `clip_segment` in `pipeline.hpp`: a
**Cohen–Sutherland** clip of every line to the framebuffer rectangle *before* the int
cast. It bounds the loop, removes the UB, and — unlike a naive per-axis clamp —
**preserves the line's slope**. All three line paths (`draw_lines`, the `draw_mesh`
wireframe, and `draw_wire`) route through it. The triangle rasterizer already clamped its
bounding box (Chapter 19 §4); this closes the same gap for lines.

---

## 6. Acceptance checklist (maps to `requirements.md` §8/§10)

- [x] **Create** several objects of multiple shapes (cube/sphere/plane/cylinder).
- [x] **Select** by mouse picking; **Tab** to cycle.
- [x] **Transform**: translate (drag on ground), lift (Q/E), rotate (arrows), scale (−/=).
- [x] **Delete** the selection.
- [x] **Camera**: orbit (right-drag), pan (middle-drag), zoom (W/S), focus (F).
- [x] **Toggles**: shading (wire/flat/Gouraud), grid, axes, culling.
- [x] **`ctest` green** (math, render3d, **viz3d**, chess, fps); headless `--viz3d`
  exit 0; **0 leaks**; warning-clean; no SDL above the platform layer.

`test_viz3d` covers the model and math headlessly: `ray_sphere`/`ray_plane_y`, the
camera pick ray, and `Editor` spawn/pick/cycle/remove. The visual result was verified by
scripting spawns + drags offline and rendering a frame (three shapes placed at distinct
positions, the selection highlighted, the HUD reporting `objects:3`).

---

## 7. Known scope (honest)

Deliberately left for later: multi-select, undo/redo, on-object gizmo handles, and
**save/load** (M4 introduces serialization). Picking uses bounding spheres, not exact
triangles. Each is a clean extension point — and a Chapter 24 exercise.

---

## 8. What's next

The sandbox is the bridge from "engine" to "tools." With a tested 3D core and an
interactive scene that creates and manipulates geometry, the natural next steps are
**M4** (the isometric sim — depth-sorting, A* pathfinding, and the serialization that
brings save/load here too) and eventually **M5** (the WebAssembly port — which, thanks to
the platform seam and the all-CPU framebuffer, runs this exact sandbox in a browser
without touching engine code).

## 9. Glossary

- **Sandbox / editor** — an interactive scene for creating and manipulating content.
- **Model vs. shell** — pure data+rules (`Editor`) vs. thin I/O glue (`EditorScene`).
- **Mesh template** — one shared mesh drawn many times with different model matrices.
- **Selection highlight** — a visual marker (here a yellow wire overlay) for the active
  object.
- **Cohen–Sutherland** — a classic algorithm for clipping a segment to a rectangle.

## 10. Exercises

1. **Duplicate.** Add a key that clones the selected object (offset a little) — one
   `spawn` with the selection's fields.
2. **Color cycle.** Add a key that recolors the selected object through the palette.
3. **Snap & align.** Combine the Chapter 24 grid-snap exercise with a key that zeroes an
   object's rotation, for tidy layouts.
4. **Save/load (preview of M4).** Serialize the object list to a text file and reload it
   through the `assets` seam.
