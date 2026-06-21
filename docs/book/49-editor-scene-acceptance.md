# Chapter 49 — Widgets, the Editor Scene & the Whole Project

> **What this is.** Subsystem **F**, part two — the widgets in detail and the
> `--editor` capstone that ties the GUI to the physics engine — followed by the F
> acceptance and a recap of the **entire** project (M0–M5 + the A→F engine-core
> program + the native webserver). Code: `src/engine/ui/ui.cpp`,
> `src/games/editor/editor_scene.cpp`.

---

## 1. The widgets in detail

All three interactive widgets share the hot/active core (ch48); they differ only in how
they draw and what they write.

- **checkbox(rect, label, bool& value):** a button over the box that, on click, flips
  `value` and returns whether it toggled. The box fills with an accent square when on.

- **slider(rect, label, float& value, lo, hi):** while it's the active widget and the
  mouse is down, map the mouse x across the track to `[lo, hi]` (clamped) and write
  `value`; returns whether the value changed. Note it begins dragging on the press
  frame, so a single click also nudges the value — intentional and tested.

```cpp
if (active_ == id && in_.down && r.w > 0) {
    float t  = clampf(float(in_.mx - r.x) / float(r.w), 0, 1);
    float nv = lo + t * (hi - lo);
    if (nv != value) { value = nv; changed = true; }
}
```

- **button/label/panel:** button returns a click; label just draws text; panel draws a
  translucent backdrop (per-pixel `blend_pixel`) + title and seeds the layout cursor.

Every draw is `if (r_)`-guarded, so `tests/test_ui.cpp` exercises all of this with a
**null renderer** and synthetic two-frame input sequences (press→release in/out of the
rect, drag/clamp, hover-doesn't-change).

## 2. The `--editor` capstone

`EditorScene` puts the engine's pieces on screen together:

- A UI **panel** — body count + fps, **Spawn Circle** / **Spawn Box** buttons, a
  **gravity** checkbox, a **restitution** slider, and **Reset**.
- A **physics sandbox** (subsystem E): a static floor + two walls; clicking the world
  (when not over the panel) drops a body of the current kind at the cursor; bodies fall,
  collide, and pile up.

Immediate mode couples input and drawing, so the UI runs in `render()` (once per frame,
where both the renderer and input are available); physics advances in `update()` at the
fixed step:

```cpp
void EditorScene::update(double dt, const InputState&) {
    world_.set_gravity({0, gravity_on_ ? kGravity : 0});
    world_.step(dt);                                   // E: simulate
}
void EditorScene::render(const Context& ctx) {
    draw_all_bodies(ctx.gfx);
    ui_.begin(&ctx.gfx, adapt(ctx.input));
    ui_.panel({12,12,210,220}, "EDITOR");
    if (ui_.button("Spawn Circle")) spawn(Circle);
    ui_.checkbox("gravity", gravity_on_);
    ui_.slider("restitution", restitution_, 0, 1);
    if (ui_.button("Reset")) reset_world();
    ui_.end();
    if (ctx.input.pressed(Left) && !ui_.hovering_ui()) spawn_at(cursor);   // world click
}
```

The world-click uses the **press** edge while buttons act on **release**, and
`hovering_ui()` excludes the panel — so one click is either a UI action or a world
drop, never both. (Verified by an offline render: the panel plus a row of dropped
circles resting on the floor.)

## 3. F acceptance

- [x] **Immediate-mode GUI** with a correct hot/active model: clicks fire on release of
      the pressed widget; drags tracked via `active`.
- [x] **Widgets**: button, checkbox (toggle), slider (drag + clamp + change flag),
      label, panel — all hand-drawn via `Renderer2D`, no SDL.
- [x] **Headless-testable**: logic runs with a null renderer; `ctest ui` green; ASan/
      UBSan clean.
- [x] **Editor capstone**: `--editor` wires the UI to the physics sandbox; click to drop
      bodies, edit gravity/restitution, reset.

Verified by `tests/test_ui.cpp` (button click in/out, checkbox toggle + release-outside,
slider drag/clamp/zero-width, hovering flag) and the offline editor render.

## 4. The whole project — recap

With F merged, the full arc of `requirements.md` and the post-M5 program is complete:

| Phase | What shipped |
|------|--------------|
| **M0** | Engine foundation: platform seam, framebuffer, fixed-timestep loop, math, 2D renderer, input |
| **M1** | Chess — full rules + minimax/alpha-beta AI, GUI **and** TUI, real `.hrt` piece art |
| **M2** | FPS raycaster — textured walls, billboard sprites, audio |
| **M3 / M3.5** | Real 3D core (software rasteriser, z-buffer, perspective, cameras) + interactive sandbox |
| **M4** | Isometric farm sim — tile map, depth sort, ECS, A*, save/load |
| **M5** | WebAssembly port — chess + 3D core in the browser, **no engine rewrite** |
| **ext** | Native webserver — hand-written HTTP, serves the WASM build + a leaderboard |
| **A–F** | Engine-core systems: **allocators · ECS · job system · asset pipeline+hot reload · 2D physics · immediate-mode GUI/editor** |

Every subsystem was **hand-written** (SDL only at the platform seam, every pixel ours),
**reviewed** (cpp/security/concurrency passes with fixes), **tested** (13 CTest suites,
ASan/UBSan/TSan clean), and **documented** as a textbook chapter. The architecture held:
SDL never leaked above the platform, the loop stayed a `tick(dt)`, I/O stayed behind the
asset seam — which is exactly why the web port and the new subsystems dropped in
cleanly.

## 5. Where to go next (all optional)

- **Wire the systems together more:** `Body` as an ECS component; `parallel_for` the
  physics broadphase; the editor inspecting live ECS entities; hot-reloaded sprites in
  a game.
- **Deepen any subsystem** via its chapter's exercises (archetype ECS, fiber jobs,
  rotation+friction physics, an id-stack GUI, event-driven hot reload, web persistence).
- **Build a game** on the full stack — every piece needed is now in place and explained.

## 6. Glossary

- **Capstone** — a demo combining several subsystems (here, GUI + physics + renderer).
- **Adapter** — converting `platform::InputState` to `ui::Input` at the boundary.
- **hovering_ui** — "the mouse is over the panel"; lets the world ignore UI clicks.

## 7. Exercises

1. **Inspector.** List the physics bodies in a scrollable panel; click one to select and
   edit its restitution via a slider.
2. **Drag to move.** Let the user grab a body with the mouse and fling it (set velocity
   from mouse delta).
3. **Save the scene.** Serialize the sandbox bodies (subsystem D's pattern) and reload
   them with a button.
4. **Your game.** Pick any chapter's engine and build something new on it — that was the
   whole point.

*(Subsystem F complete — and with it, the engine-core program A→F. The project is done.)*
