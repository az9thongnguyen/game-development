# Chapter 26 — Isometric Projection

> **What this is.** The first chapter of M4. We leave real 3D behind and learn a
> flat 2D drawing trick that *looks* 3D: the **isometric diamond grid**. By the end
> you will understand the two coordinate transforms (grid→screen and screen→grid),
> why a tile is twice as wide as it is tall, and how a single mouse click turns into
> "the player tapped tile (5, 3)". The whole chapter rests on one tiny header,
> `src/engine/iso.hpp`, that is pure math — no SDL, no renderer, no z-buffer.

---

## 1. Concept: isometric is *not* 3D

In Chapter 18 we built a real perspective pipeline: vertices went through model →
view → projection → perspective divide → a z-buffer decided what was visible.
Isometric rendering throws *all* of that away. There is:

- **no camera matrix** — just a 2D pixel offset,
- **no perspective divide** — parallel lines stay parallel,
- **no z-buffer** — the only notion of depth is the *order* we paint things in
  (Chapter 27).

What you get instead is the classic "¾ overhead" look of *SimCity 2000*,
*Age of Empires II*, *Diablo*, and *Stardew Valley*-adjacent games. It is cheap
(everything is a 2D blit/fill), it is crisp (no filtering artifacts), and it is a
beautiful teaching vehicle for **coordinate transforms** because the math is small
enough to do on paper.

The trick: take a square tile grid seen from straight above, then **rotate it 45°
and squash it vertically to half height**. A square becomes a 2:1 **diamond
(rhombus)**. Lay those diamonds edge to edge and the brain reads a tilted plane.

```
   square grid (top-down)          isometric (rotate 45°, squash y by 2)

   +--+--+--+                              /\
   |  |  |  |                            /    \
   +--+--+--+            ==>            /  /\    \
   |  |  |  |                          \ /    \ /
   +--+--+--+                           \  \/  /
                                          \  /
                                           \/
```

## 2. The diamond and its two numbers

Our tile is `kTileW = 64` pixels wide and `kTileH = 32` pixels tall. The 2:1 ratio
is the defining choice of "2:1 isometric" (technically *dimetric*, but everyone
says isometric). A single diamond has four corners:

```
            N  (cx, cy - H/2)
           /  \
   W -----+    +----- E      W = (cx - W/2, cy)   E = (cx + W/2, cy)
  (left)   \  /   (right)    H = kTileH = 32,  W = kTileW = 64
            S  (cx, cy + H/2)
```

`(cx, cy)` is the **center** of the diamond — that is the anchor we compute for each
grid cell. Note half-width is 32 and half-height is 16.

## 3. Grid → screen (the forward transform)

This is the heart of everything. Cell `(gx, gy)` maps to screen center:

```
sx = ox + (gx - gy) * (kTileW / 2)
sy = oy + (gx + gy) * (kTileH / 2)
```

`(ox, oy)` is the **camera offset** — the screen pixel where cell (0,0) sits. Panning
the camera just changes `ox, oy`.

Read the two formulas as directions:

- **`+gx`** adds `(+32, +16)` → moves **down-right** on screen.
- **`+gy`** adds `(-32, +16)` → moves **down-left** on screen.

So increasing either grid axis moves the tile *down* the screen (toward the
viewer), one to the right and one to the left. That is exactly the ¾ view.

In code (`src/engine/iso.hpp`):

```cpp
inline ScreenPt grid_to_screen(float gx, float gy, float ox, float oy) {
    return ScreenPt{ ox + (gx - gy) * (kTileW * 0.5f),
                     oy + (gx + gy) * (kTileH * 0.5f) };
}
```

It takes **floats**, not ints. Why? Because a moving agent (the farmer in Chapter
29) sits at a *fractional* cell like `(2.5, 1.5)` mid-step, and we want its pixel
position to glide smoothly. Integer cells are just the common case.

### Worked example

Camera at `(ox, oy) = (480, 80)`. Where is cell `(3, 1)`?

```
sx = 480 + (3 - 1) * 32 = 480 + 64  = 544
sy = 80  + (3 + 1) * 16 = 80  + 64  = 144
```

And cell `(1, 3)` (swap the coordinates)?

```
sx = 480 + (1 - 3) * 32 = 480 - 64  = 416
sy = 80  + (1 + 3) * 16 = 80  + 64  = 144
```

Same `sy` (both have `gx+gy = 4`), mirrored `sx`. They sit on the same horizontal
"row" of the diamond lattice — a fact Chapter 27's depth sort leans on.

## 4. Screen → grid (the inverse transform)

Picking — "which tile did the mouse click?" — needs the inverse. We have two
equations and two unknowns. Define the intermediate quantities:

```
fx = (sx - ox) / (kTileW / 2) = gx - gy
fy = (sy - oy) / (kTileH / 2) = gx + gy
```

Add and subtract to separate `gx` and `gy`:

```
fx + fy = (gx - gy) + (gx + gy) = 2·gx   →   gx = (fx + fy) / 2
fy - fx = (gx + gy) - (gx - gy) = 2·gy   →   gy = (fy - fx) / 2
```

Then **floor** to land on a whole tile:

```cpp
inline Vec2i screen_to_grid(float sx, float sy, float ox, float oy) {
    const float fx = (sx - ox) / (kTileW * 0.5f);   // gx - gy
    const float fy = (sy - oy) / (kTileH * 0.5f);   // gx + gy
    const float gx = (fx + fy) * 0.5f;
    const float gy = (fy - fx) * 0.5f;
    return Vec2i{ static_cast<int>(std::floor(gx)),
                 static_cast<int>(std::floor(gy)) };
}
```

`std::floor` (not a cast-to-int) is essential: a cast truncates **toward zero**, so a
click at `gx = -0.3` would truncate to `0` instead of flooring to `-1`, and tiles
just off the top-left edge would mis-pick. Floor rounds toward −∞, which is what
"which cell contains this point" means.

### Why this inverse is exact for integer cells

Feed a cell center back through both transforms and the algebra cancels perfectly:
`fx = gx - gy`, `fy = gx + gy`, so `gx' = ((gx-gy)+(gx+gy))/2 = gx` exactly (no
rounding for small integers in `float`). The unit test `test_projection` in
`tests/test_iso.cpp` checks this round-trip for a 6×6 block of cells.

## 5. Code walkthrough: the whole header

`iso.hpp` is ~40 lines of real code. Besides the two transforms it defines:

```cpp
struct Vec2i { int x = 0, y = 0; };          // an integer grid coordinate
struct ScreenPt { float x = 0.0f, y = 0.0f; }; // a pixel position

inline float depth_key(float gx, float gy) { return gx + gy; }
```

- **`Vec2i`** is the shared "grid cell" type, reused by the tile map, the ECS, and
  A* — defining it once here keeps those modules from inventing three slightly
  different versions.
- **`depth_key`** is `gx + gy`: the painter's-algorithm sort key. Chapter 27 explains
  why that single number is enough to order the whole scene.

The header is deliberately **render-free and SDL-free**. That is what lets
`tests/test_iso.cpp` exercise projection and picking with zero graphics, and it is
the same discipline that kept the chess rules and the A* planner testable.

## 6. Run & observe

`--iso` launches the farm sandbox (Chapter 31 covers the controls). For *this*
chapter, the thing to watch is the bottom HUD line:

```
brush:tree  entities:30  tile:(5,3)  fps:60
```

Move the mouse around. `tile:(x,y)` is `screen_to_grid` running live every frame on
the cursor. Slide off the grid and the numbers go negative or exceed the map size —
that is the inverse transform faithfully reporting cells that don't exist, which the
renderer simply declines to highlight (an `in_bounds` check). Pan with the arrow
keys and watch the same screen pixel report different tiles as `ox, oy` shift.

## 7. Pitfalls

- **Truncation vs. floor.** Casting `(int)gx` instead of `floor` breaks picking for
  negative cells. We hit this and fixed it before it shipped — see §4.
- **Off-by-half on the anchor.** `grid_to_screen` returns the diamond *center*, not
  its top corner. If you draw a sprite with its top-left at `(sx, sy)` it will sit
  too low and too far right. Anchor art to the center (or the bottom tip for
  "standing on the tile").
- **Forgetting floats.** If `grid_to_screen` took `int`, the farmer would teleport
  cell-to-cell instead of gliding. Fractional input is a feature, not an accident.
- **Float→int range.** `static_cast<int>(std::floor(x))` is UB if `x` is outside
  `int` range. We clamp the camera pan (Chapter 31) so the argument can never get
  that large — a real fix prompted by the code review.

## 8. Glossary

- **Isometric / dimetric** — a parallel (non-perspective) projection that fakes a 3D
  overhead view; our 2:1 tiles are technically dimetric.
- **Diamond / rhombus** — the on-screen shape of one isometric tile.
- **Camera offset `(ox, oy)`** — the screen pixel of grid cell (0,0); panning edits it.
- **Forward / inverse transform** — grid→screen (to draw) and screen→grid (to pick).
- **Depth key** — `gx + gy`; the painter's-algorithm ordering value (Chapter 27).

## 9. Exercises

1. **Zoom.** Add a `scale` to `Camera2D` and multiply `kTileW/2`, `kTileH/2` by it in
   both transforms. Verify picking still round-trips (divide by `scale` in the
   inverse).
2. **A different ratio.** Change `kTileH` to 24 (a steeper "2.66:1" look). What
   breaks visually? (Hint: the object art heights are tuned to 32.)
3. **Hex grid.** Sketch the forward transform for flat-top hexagons. Why is the
   inverse much harder than for diamonds? (This is why most 2D builders pick
   diamonds.)
4. **Diagnostic overlay.** Draw the picked tile's `(gx, gy)` as text floating at its
   screen center, so you can *see* the inverse transform agreeing with the grid.
