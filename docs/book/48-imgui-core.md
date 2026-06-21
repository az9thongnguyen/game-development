# Chapter 48 — Immediate-Mode GUI: the Idea & the Core

> **What this is.** Subsystem **F**, part one (and the last subsystem): a hand-written
> **immediate-mode GUI** — the toolkit any editor needs. We cover *why* immediate mode,
> the **hot/active** interaction model that makes it work, widget ids, the frame
> lifecycle, and the layout cursor. Widgets and the editor scene are chapter 49. Code:
> `src/engine/ui/ui.{hpp,cpp}`.

---

## 1. Retained vs immediate mode

Two ways to build UI:

- **Retained mode** (Qt, the browser DOM): you *create* widget objects once, they live
  in a tree, you wire callbacks, and you mutate them later. Powerful, but heavy:
  lifetimes, synchronisation between your data and the widget state, lots of glue.
- **Immediate mode** (Dear ImGui): there is no tree. Each frame you *call* a function
  that both draws the widget and returns its result:

```cpp
if (ui.button(r, "Spawn")) spawn();      // drawn AND tested, this frame
ui.checkbox(r, "gravity", gravity_on);   // reads+writes your bool directly
```

State lives in **your** variables, not the UI. There's nothing to keep in sync, nothing
to free. It's tiny and ideal for tools, debug overlays, and editors — which is exactly
what subsystem F is for. The cost is that everything is redrawn every frame (cheap for a
software renderer at editor scale).

## 2. The hot/active model (the whole trick)

How does a stateless-looking call know it was clicked, across the press→release that
spans two frames? Two pieces of retained state in the Context:

- **`hot`** — the id of the widget under the mouse *this frame* (recomputed each frame).
- **`active`** — the id of the widget the mouse *pressed down on* (persists across
  frames until release — this is what tracks a drag).

The rules:

```
on a widget with rect R and id ID:
  if mouse over R:                 hot = ID
  if active == ID and released:    it's a CLICK if still over R;  active = 0
  else if hot == ID and pressed:   active = ID         (begin interacting)
```

A **click fires on release, only if the same widget was the one pressed**. That single
rule gives correct behavior for the tricky cases: press a button then drag off and
release → no click; press, drag, release back on it → click; press button A, release
over button B → neither clicks.

```cpp
bool Context::button(Rect r, const char* label) {
    const std::uint32_t id = id_of(label);
    const bool over = point_in(r);
    if (over) { hot_ = id; hovering_ = true; }
    bool clicked = false;
    if (active_ == id) { if (in_.released) { clicked = over; active_ = 0; } }
    else if (over && in_.pressed) active_ = id;
    /* …draw… */
    return clicked;
}
```

## 3. Widget ids (and the collision caveat)

`active`/`hot` are *ids*, not pointers. We derive an id by hashing the widget's label
(FNV-1a, forced nonzero so 0 means "nothing"):

```cpp
std::uint32_t Context::id_of(const char* s) {
    std::uint32_t h = 2166136261u;
    for (; s && *s; ++s) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h ? h : 1u;
}
```

**Caveat (documented contract):** two widgets with the *same label* get the *same id*
and their interactions collide — a click can be swallowed. Keep labels unique within a
frame. Real ImGui solves this with an **id stack** and `"##suffix"` ids; that's an
exercise here. For an editor panel with a handful of distinct buttons it's a non-issue.

## 4. The frame lifecycle

```cpp
ui.begin(&renderer, input);   // store input; reset hot_ = 0; hovering_ = false
//   …call widgets: they read input, set hot_/active_, draw, return results…
ui.end();                     // if the button is up, clear active_ (safety)
```

`begin` snapshots the frame's mouse state (position + the three edges: down / pressed /
released) and clears `hot_` (it's recomputed by whichever widget the mouse is over).
`active_` deliberately **survives** across `begin` so a drag in progress is remembered.
`end` clears `active_` if the mouse is up — a belt-and-suspenders guard so nothing stays
stuck active.

Because the **renderer pointer may be null**, all drawing is `if (r_)`-guarded — the
*logic* (hot/active/return values) runs perfectly headless, which is how the unit tests
drive widgets with synthetic input and assert their behavior without a window.

## 5. Layout: a vertical cursor

Specifying a `Rect` for every widget is tedious, so there's a layout layer. `panel`
sets a cursor inside a background rectangle; each layout widget claims a row at the
cursor and advances it down:

```cpp
ui.panel({12,12,210,220}, "EDITOR");   // cursor → just inside, below the title
ui.label("bodies: 26");                // claims a row, cursor moves down
if (ui.button("Spawn Circle")) …;      // next row
ui.checkbox("gravity", gravity_on);    // next row
```

Each layout widget is a thin wrapper that computes a `Rect` from the cursor and calls
the explicit-rect version — so the *core logic is shared and tested once*, and layout is
just convenience.

## 6. Pitfalls

- **Forgetting `begin`/`end`.** Without them `hot_`/`active_` are never managed; nothing
  clicks.
- **Duplicate labels.** Same id → collision. Keep them unique (§3).
- **Querying results after mutating input.** Read a widget's return right where you call
  it (that's the immediate-mode style).
- **Holding a widget pointer.** There are none — that's the point.

## 7. Glossary

- **Immediate mode** — call-to-draw-and-test UI; no retained widget tree.
- **hot / active** — widget under the cursor / widget being pressed-and-held.
- **Widget id** — hash of the label; identifies hot/active across frames.
- **Frame lifecycle** — `begin` (snapshot input, reset hot) → widgets → `end` (clear
  stuck active).
- **Layout cursor** — a running position that auto-places stacked widgets.

## 8. Exercises

1. **Headless click.** In a test, feed a press-then-release over a button rect and
   assert it returns `true`; release off it and assert `false`.
2. **An id stack.** Add `push_id(int)`/`pop_id()` mixed into `id_of` so duplicate labels
   under different ids no longer collide.
3. **Keyboard focus.** Add a focused-widget id and Tab-to-cycle; let Enter "click" the
   focused button.
4. **A new widget.** Add `int_stepper(label, int& v, lo, hi)` with − / + buttons reusing
   `button`.

*(Next: chapter 49 — widgets, the editor scene, and the project recap.)*
