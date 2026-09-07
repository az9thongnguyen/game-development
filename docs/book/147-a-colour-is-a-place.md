# 147 — A colour is a place

Two lines had been sitting in `PROGRESS.md` under *deliberately deferred* since the
Pixels editor grew a colour mixer:

> **không có ô vuông S/V 2D** (ba slider, vì `ui::hit` báo click chứ không báo drag) ·
> **màu đã pha không có nhà**

*No 2D saturation/value square (three sliders, because `ui::hit` reports a click and not
a drag)* and *a mixed colour has nowhere to live*.

The first is a blocker that had already dissolved and nobody had noticed. The second is
a small hole with an annoying shape.

---

## The blocker that was gone

`ui::hit` does report a click — `interact()` fires on release-over-the-rect, which is
what a button is. But `slider` never used it: it has its own press-hold-release loop,
because a slider stays active while the pointer is **held**, wherever it wanders to.
Chapter 144's `splitter` copied that loop. This would have been the third.

Three copies of a state machine is three chances for one of them to keep `active_` after
the button is up. So the loop moved out:

```cpp
bool Context::drag_in(Id id, Rect r, bool focusable) {
    const bool over = point_in(r) && !inert_;
    if (focusable && !inert_) tab_order_.push_back(id);
    if (over) { hot_ = id; hovering_ = true; }

    if (active_ == id) {
        if (!in_.down) active_ = 0;
    } else if (over && in_.pressed) {
        active_ = id;
        if (focusable) focused_ = id;
    }
    return active_ == id && in_.down;
}
```

That is the extraction this repo's rule allows: **not one consumer and a hope, but two
that already existed and a third arriving.** `slider`, `splitter` and `xy_pad` are now
one loop.

`xy_pad` itself is deliberately thin. It owns the pointer and the arrow keys, draws a
crosshair, and knows nothing about what is under it:

```cpp
bool xy_pad(const char* id, Rect r, float& x, float& y);
```

Two details are worth the words they cost. `y` runs **downward**, the direction the
framebuffer runs, so the caller does the one flip a colour picker needs (`v = 1 - y`) in
a place a reader can see it — a pad that flipped internally would put the surprise inside
the widget. And the arrows nudge both axes: a control only a mouse can reach is the thing
this project keeps going back to fix.

## A colour is a place

Saturation and value were two sliders. They are one rectangle now, because they are one
choice — a colour is a place, and picking it as two numbers means finding the same shade
twice.

The gradient is painted by the **caller**, not the widget. A widget that painted a colour
ramp would be a colour picker pretending to be a primitive, and the next caller — a
gradient editor, a map — would have to argue it out of doing so.

### It had to fit in the height the sliders freed

This panel is not a scrolling one. It computes `inspector_clipped_` and says so on the
status strip when Save falls below the fold. So the new control's budget was exactly what
the two sliders gave back: 64 pixels. A feature that pushed Save off the bottom would be
a feature that broke a verb, and the test that caught the first attempt at 96 was the one
that clicks Save.

### And then the picture

At 64×64 it was a square, left-aligned, with a hand's width of empty panel beside it. It
read as unfinished. Nothing in the code says so; no assertion could; the frame said it in
a second.

It is the panel's width now and 64 tall, lined up with the hue slider above it. The two
axes are no longer equally sensitive — which is true of every colour picker anyone has
ever used, and the "both axes read alike" argument that produced the square was invented
to justify the square rather than the other way round.

Five chapters running where a bug or a bad decision was visible only in a rendered frame.

---

## The home

A mixed colour was the brush and nothing else. Wander to a shade, paint with it, pick a
neighbouring colour to compare, and the shade was gone — the only way back was to paint a
pixel with it first and eyedropper it afterwards.

**Keep** puts it in the palette. Not a second palette: the same one, which is rebuilt
from the **image** every time the file opens. Which gives the rule for free, and it is a
rule nobody has to be told:

> a mixed colour survives a reload exactly when you actually used it.

The button sits on the hex row rather than on one of its own, for the height reason
above, and it is **disabled** when the colour is already a swatch — with a tooltip that
says which of the two it is. A disabled button that still fires is the same lie as a live
one that does nothing, so the test presses it in both states.

---

## What is verified, and what is not

Verified:

- **95/95 `ctest`, twice.**
- `xy_pad`: both axes clamp at both ends **and lift again**, a drag that lands where it
  already is is not a change, a press outside does not grab it, release ends it, the
  arrows move both axes both ways and stop at the edges, and inert blocks the pointer
  **and** the keyboard.
- The mixer holds its own coordinates, which is what makes a drag through black
  reversible — the assertion that has guarded this since the mixer existed, now pointed
  at the square's vertical axis instead of a slider.
- **Keep**: adds the colour, refuses a duplicate, comes back when the colour moves off
  the palette, and does not sit on the hex field it shares a row with.
- **A rendered frame looked at**, twice — which is the only reason the square is a
  rectangle.

Not verified:

- **Nobody has picked a colour with a hand here.** Every drag is a synthetic pointer.
  Whether 64 pixels of vertical range is enough to land on a shade you meant is a
  question a synthetic drag cannot ask.
- The gradient is **one `fill_rect` per pixel**, ~11,500 a frame at this size. That is
  well inside budget on this machine and has not been measured on a slow one; the Studio's
  bench does not open the Pixels tab, so it is not in any recorded number.
- **The palette still has no order and no removal.** A kept colour goes on the end, and
  the only way to get rid of one is to reopen the file. Dropping a swatch is a verb this
  editor does not have.
- `mix_slider(1)` and `mix_slider(2)` now return empty rects — deliberately, so a caller
  still reaching for the old sliders fails loudly. Nothing outside the tests reached for
  them, which is checked by the tests failing when they did.
