# Subsystem F — Editor Support (Immediate-Mode GUI) — Design Spec

> Date: 2026-06-21 · Program A→F, step F (last) · Branch `feat/editor`
> The missing toolkit for any editor: a hand-written immediate-mode GUI drawn into the
> framebuffer, plus an editor scene that uses it to drive a live physics sandbox (E).

## 1. Goal & scope

"Editor support" needs, first, a way to draw and interact with **UI widgets** — the
engine has none. So subsystem F builds a small **immediate-mode GUI (IMGUI)**:
buttons, checkboxes, sliders, labels, panels — drawn entirely by hand via `Renderer2D`
(no SDL widgets), driven by the normalized input. Then a `--editor` scene wires it to a
**physics sandbox** (subsystem E) as a tangible demo: spawn bodies, edit gravity and
restitution, reset.

Scope chosen for "testable + bounded": the IMGUI *logic* (hot/active item, click,
slider value) is unit-tested with synthetic input and no window. A full docking/panel
editor, an ECS inspector with reflection, and gizmos are documented extensions.

## 2. Why immediate-mode

Immediate mode = no retained widget tree; each frame you *call* `button("Spawn")` and it
both draws and returns whether it was clicked. State lives in the caller, not the UI.
It's tiny, perfect for tools/debug overlays, and famously how Dear ImGui works. The
core is the **hot/active** item model:

- **hot** = the widget under the mouse this frame.
- **active** = the widget the mouse pressed down on (held until release).
- A **click** = release while hot == active.

## 3. Files

```
src/engine/ui/ui.hpp / ui.cpp   (namespace ui)
  Input { mx, my; down, pressed, released }   (adapted from platform::InputState)
  Context: begin(renderer, input) / end(); hot/active id tracking; a vertical layout
  widgets — explicit-rect (core, testable) + layout-cursor (convenience):
    button, checkbox, slider, label, panel
src/games/editor/editor_scene.hpp / .cpp   --editor: UI + physics sandbox
tests/test_ui.cpp
docs/book/48,49 (split)
```

CMake: `ui_core` static lib (ui.cpp) — references Renderer2D symbols provided by the
linking target (like render3d_core). `test_ui` compiles ui.cpp + renderer2d.cpp with a
heap framebuffer (drawing allowed but optional — a null renderer runs logic headless).
demo links ui_core + physics_core + editor_scene.cpp; `--editor` dispatch in main.

## 4. The Context

```cpp
namespace ui {
struct Rect  { int x, y, w, h; };
struct Input { int mx=0, my=0; bool down=false, pressed=false, released=false; };

class Context {
public:
    void begin(gfx::Renderer2D* r, const Input& in);  // r may be null (headless tests)
    void end();
    // explicit-rect widgets (core; deterministic to test):
    bool button(Rect r, const char* label);
    bool checkbox(Rect r, const char* label, bool& value);   // returns true if toggled
    bool slider(Rect r, const char* label, float& value, float lo, float hi); // true if changed
    void label(int x, int y, const char* text, gfx::Color c = ...);
    // layout helpers (advance a vertical cursor inside a panel):
    void panel(int x, int y, int w, const char* title = nullptr);
    bool button(const char* label);
    bool checkbox(const char* label, bool& v);
    bool slider(const char* label, float& v, float lo, float hi);
    void label(const char* text);
};
}
```

Widget **id** = hash of the label (nonzero). `hot_id` recomputed each frame in
`begin`; `active_id` persists across frames while dragging. `begin` stores the frame's
mouse state; `end` clears `active_id` if the button is up (safety).

## 5. Widget behavior (the core logic)

```cpp
// button(rect, label):
bool over = point_in(in_, rect);
if (over) hot_id_ = id;
bool clicked = false;
if (active_id_ == id) { if (in_.released) { clicked = over; active_id_ = 0; } }
else if (hot_id_ == id && in_.pressed) active_id_ = id;
// draw: base / hover / pressed color + centered label (if renderer present)
return clicked;
```

- **checkbox:** a button over the box toggles `value`, returns the change.
- **slider:** while `active_id == id` and mouse down, map `mx` across the track to
  `[lo,hi]` (clamped); returns true if the value changed. Press starts the drag.

All drawing is guarded by `if (r_)`, so the logic runs headless in tests.

## 6. The `--editor` scene (capstone)

A physics sandbox driven by the UI:
- A UI panel: **Spawn Circle** / **Spawn Box** buttons, a **gravity** checkbox, a
  **restitution** slider, a **Reset** button, and a live body count.
- Click in the world (outside the panel) drops a body at the cursor with the current
  settings; bodies fall, collide, bounce against a floor and walls.
- Renders bodies (circles/boxes) + the panel via `Renderer2D`.

This visibly ties together E (physics), F (UI), and the 2D renderer — the program's
finale. Verified by an offline render (BMP→PNG) like the other scenes.

## 7. Correctness focus
- hot/active/click model: click only fires on release while hot==active; press-then-
  drag-off-then-release ≠ click; active persists during a drag.
- slider maps + clamps correctly; returns changed only when the value actually moves.
- checkbox toggles exactly once per click.
- headless (null renderer) runs all logic without crashing (for tests).
- input adaptation from platform::InputState is lossless (mouse pos + 3 edges).

## 8. Tests (`tests/test_ui.cpp`)
explicit-rect widgets with synthetic two-frame input sequences: button click (press
inside → release inside = click; release outside = no click); checkbox toggle; slider
drag sets/clamps value + change flag; hot/active transitions; point_in_rect. Null
renderer (headless). ASan+UBSan.

## 9. Guidebook (split)
- **48 — Immediate-mode GUI: the idea & the core** (retained vs immediate, the
  hot/active model, ids, the frame lifecycle, layout).
- **49 — Widgets & the editor scene** (button/checkbox/slider internals, drawing, the
  `--editor` physics sandbox; F acceptance + the whole A→F recap).

## 10. Risks
- id collisions from label hashing → unlikely at editor scale; documented (real ImGui
  uses an id stack — an exercise).
- The editor scene needs a window → its logic stays in the testable UI core; the scene
  is verified by offline render, like the others.
