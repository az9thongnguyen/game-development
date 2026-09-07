# 144 — The comment that named its own slice

Chapter 112 wrote the Studio's Edit section and left a note over the two lines that
decide how wide the inspector is:

```cpp
// The Map section: canvas on the left, inspector on the right, status strip under
// both. The split is fixed — a draggable one needs a cursor shape, a hit zone and a
// persisted position, and no second author has asked for it yet.
```

That is a good comment. It does not say "TODO"; it says what the feature would cost and
why the cost was not worth paying yet. Thirty-two chapters later there is a second
author, and the note turns out to have named its own slice: a cursor shape, a hit zone,
a persisted position. All three, and the third is the only one with a design in it.

Two other pieces of Studio debt came along, because they are the same seam: the status
strip under the split, and the grid the scene canvas never had.

---

## The persisted position, which is a decision and not a number

The easy version is one line: remember the width, restore it on open. Then somebody
opens the Studio on a laptop screen, the saved 900px inspector does not fit, and the
Studio has to do *something*. Whatever it does, it does again on the next window, and
the next.

The rule this file settled on:

> **The stored width is never clamped. The drawn width always is.**

```cpp
[[nodiscard]] inline int fit_inspector(int stored, int body_w) {
    const int most  = body_w / 2;                       // the canvas is never the smaller half
    const int least = std::min(kMinInspector, most);
    if (most <= 0) return 0;
    return std::clamp(stored, least, most);
}
```

`fit_inspector` is a function of the window, so nothing about a narrow frame survives it.
Shrink the window and the panel narrows; grow it back and the width you dragged to comes
back whole. Clamping on the way *in* — writing the narrowed number to disk — would look
identical for as long as the window stayed small, and would have quietly destroyed the
layout the first time somebody resized.

That is the shape the memory file calls *guards have a reverse direction*: **the bug is
never the collision the guard prevents, it is the guard never lifting.** So the test asks
both ways, in the same three lines:

```cpp
CHECK(fit_inspector(900, 500)  == 250);   // the narrow window
CHECK(fit_inspector(900, 2400) == 900);   // ...and the wide one, afterwards
```

The width is stored **per workspace**, not once. The `Workspace` header already said why,
in chapter 116: *"260 suits a tile palette; an actor inspector with sliders wants more."*
One shared divider would move every time you changed tabs.

## The cursor shape, and a seam verb with no caller

`platform::set_cursor` has been in `platform.hpp` since the seam was written, with a
`Cursor` enum listing seven shapes. Nothing had ever called it. Not one scene, in a
hundred and forty chapters — the pointer in this project has always been an arrow.

It could not be called from where it was needed, either. A `Scene` cannot say
`platform::set_cursor(...)`: most scenes compile into headless tests that link no
backend at all, so the call would be an undefined symbol in half the test targets. The
UI layer has the same problem one level down, and already has the answer — `ui::Input`
takes *intents* (activate, cancel, copy) rather than keys, so the widgets are testable
with no SDL anywhere near them. The cursor is the same trade in the other direction:

```cpp
enum class CursorHint { Default, ResizeH };     // in ui.hpp, which knows no platform
```

A widget sets it, `Scene::cursor_hint()` reports it, and `App::frame` — the one place
that already has a window — applies it. `ui.cpp` gained a hint, `app.cpp` gained three
lines, and no scene gained a dependency.

## The hit zone, which is wider than the line

The divider draws as a 2px line, because a divider should be quiet. Its hit zone is a
finger wide, because a divider should be catchable. Those are not in tension — the
drawn thing and the touchable thing have different jobs, which is the same conclusion
`engine/ui/touch.hpp` reached about buttons — but the *cursor* is what makes the gap
honest: the pointer changes before you press, so the zone announces itself.

The drag is `slider`-shaped, not `button`-shaped: it stays active while the mouse is
**held**, not until a release over the same rect. One detail is worth its three lines:

```cpp
} else if (over && in_.pressed) {
    active_      = id;
    drag_anchor_ = in_.mx - pos;   // where INSIDE the handle it was grabbed
}
```

Without the anchor the divider jumps so its left edge lands under the cursor the instant
you press — up to a handle's width of movement before you have moved at all. The test
grabs 5px in and drags to 125, and asserts 120.

---

## The status strip: a list, not a sentence

Under the split runs a line saying what is open. Four workspaces built it, each by
concatenation, and each punctuated it differently:

```cpp
path_ + (dirty() ? "  *  unsaved" : "  saved")            // Map, Pixels
"...   [panel clipped — make the window taller]"          // Map
"%s   %zu part(s)   %zu swap(s)   %dx%d%s"                // Mixer, dirty marker LAST
```

Tidiness is not why this changed. This is:

```cpp
g.draw_text(area.x, sy, left.c_str(), ws.dirty() ? th::warn : th::text_muted);
```

One string, one colour. So in the Pixels editor, with an unsaved file, the hex code
under the cursor was drawn in the colour of a warning. The strip could say *the document
is unsaved*, or it could say *this pixel is #3aa0ff*, and whichever it said, it said in
the same voice.

A cell carries its own tone:

```cpp
struct Seg { std::string text; Tone tone = Tone::Neutral; };
```

`ui::status_bar` draws the list with a **drawn** separator — a dot, painted between
cells — so no caller punctuates anything and two workspaces cannot disagree about how
many spaces a gap is. The dirty marker is now one cell, `Tone::Warning`, and everything
beside it stays the ordinary colour. `ui::joined` is the one place a strip is flattened
back into a string, for a log or a test.

### Counting the colour

Chapter 143 ended on this and it applies again immediately: **the claim here is about
colour, and colour is invisible to every metric this repo already had.** The same cells
in one tone draw the same glyphs at the same positions — identical ink count, identical
checksum, identical frame diff. A test that watched any of those would pass a strip that
had forgotten how to warn.

So `test_ui_golden` renders the strip into a buffer of its own and counts a specific
colour, in both directions:

```cpp
CHECK(count(dirty, th::warn) > 0);      // the unsaved strip warns
CHECK(count(clean, th::warn) == 0);     // ...and the saved one does not
CHECK(count(dirty, th::text_dim) > 0);  // the cells beside it are still ordinary
```

The second line is the one that matters. Without it the check passes on a strip that
paints every cell warn — which is precisely the bug being fixed.

---

## The grid, which is arithmetic

The scene canvas had no grid and no snap. Both halves went where they belong: the
arithmetic in `sandbox_core` beside the model,

```cpp
inline float snap_to(float v, int step);   // step <= 0 = off, rounds away from zero
```

and the trigger in the workspace. Rounding is away from zero on **both** sides of the
origin, because `std::round`'s halfway rule biases one of them, and a scene laid out
around 0 would snap its left half differently from its right. The test says it as a
property over the range: `snap_to(-v, 16) == -snap_to(v, 16)`.

Placing and dragging are two call sites of the same question, and the drag is the one
that would have got it subtly wrong:

```cpp
t->x = sandbox::snap_to(wx - drag_dx_, grid_);   // the ACTOR lands on the grid...
t->x = sandbox::snap_to(wx, grid_) - drag_dx_;   // ...not the pixel you grabbed
```

The second line is a mutation in the list below, and it is killed by a test that grabs
the actor deliberately off centre. The claim that test makes is exact and says nothing
about the view's scale: **do the drag with the grid off, undo, do the identical drag with
the grid on, and the second result is the first one rounded.**

The button sits in the inspector's header, *above* the scrolling body — a setting that
decides where every click lands must not be a control you scroll to find — and the test
asks for it by name rather than by an offset:

```cpp
CHECK(click_control(d, ws, "play"));   // was: click_at(d, ws, vp.x + vp.w / 2, vp.y - 30)
```

That change is not decoration. The old line reached the Play button by arithmetic on the
viewport's rect, and the first control ever added between the header and the body — the
grid button, in this chapter — silently made those coordinates point at something else.
The test still passed. It was testing the wrong control.

---

## Green once is not green

`ctest` was green. Run immediately again, `shell_golden` failed.

The new block drags the Studio's divider and asserts the layout file records it. It
*writes* `saves/studio.layout`. On the second run the Studio therefore opened already
dragged, and "drag it 90px further" ran straight into the clamp and moved nothing.

The test was right about the product and wrong about itself, which is the only kind of
state leak a single run cannot see. It now deletes the file it is about to write. The
habit that caught it is the whole of it: **run the suite twice on purpose.**

---

## What is verified, and what is not

Verified:

- **93/93 `ctest`, run three times in a row** — the suite is clean of its own leavings.
- **25 single-token mutations, 25 killed.** The list covers both ends of the divider's
  clamp and the clamp lifting, the grab offset, the cursor hint turning on *and* off,
  the strip's per-cell colour, `fit_inspector` with and without its floor, a layout file
  from the future, an unknown key that must stay skippable, `snap_to`'s sign symmetry,
  the grid cycle returning to off, the refusal while playing lifting, and both halves of
  the persistence (never loaded / never written).
- **Two rendered frames looked at.** The Studio at a dragged split, and the status strip
  on its own — `textures/hero.hrt · unsaved · 12, 7 · #3aa0ff`, with exactly one cell
  amber.
- Golden path, Emscripten web build.

Not verified:

- **No hand has dragged this divider in a real window.** The cursor change in particular
  is asserted as a `CursorHint` value in a headless test; that the SDL backend then draws
  a resize arrow is `backend_sdl.cpp`'s twelve-line `set_cursor`, running for the first
  time ever, and nobody has watched it.
- The **full-screen lab** (`--lab scene`, `--lab map`, …) got the same splitter and reads
  the same file, but only the Studio's tab is covered by a test. They are the same object
  by construction — that is what `WorkspaceHost` is for — but "by construction" is exactly
  the argument chapters 137 through 142 kept finding holes in.
- `studio.layout` is **not versioned into anything**: it is this machine's state, under
  `saves/`, excluded from the web preload like every other file there. Nothing has tested
  what the web build does when IDBFS has one from a different window size.
- The grid is **not persisted**, deliberately — it is a way of working, not a property of
  the document. Whether that is the right call is a guess; nobody has used it for an hour.
