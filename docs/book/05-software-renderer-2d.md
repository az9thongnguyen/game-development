# Chapter 05 — The 2D Software Renderer

> **Goal of this chapter.** Finally *draw*. We build, by hand, every 2D primitive
> the games need — clearing, plotting clipped pixels, alpha blending, filled and
> outlined rectangles, lines via Bresenham, sprite blitting, and bitmap text —
> writing straight into the framebuffer. No SDL drawing. By the end the window
> shows lines, rectangles, a translucent sprite, and text, all produced by us.

---

## 1. The mental model

Everything in this chapter reduces to one operation from Chapter 02: *set the
pixel at `(x, y)` to a color* (`fb.pixels[y*pitch + x] = color`). A line is "set
the right run of pixels"; a rectangle is "set a block"; text is "set the pixels a
glyph says are lit". The art is doing it **correctly** (never writing outside the
buffer) and **cheaply** (no waste in the per-pixel hot path).

We wrap the framebuffer in a small `Renderer2D` class so scenes call
`g.draw_line(...)` instead of fiddling with indices. It's a thin, cheap object —
the `App` builds a fresh one each frame and passes it to the scene in `Context`.

---

## 2. Clipping: the non-negotiable safety rule

The framebuffer is an array of `width*height` pixels. Writing to index `y*pitch+x`
when `x` or `y` is out of range corrupts memory — maybe another variable, maybe a
crash, maybe a silent heisenbug. So **every primitive must clip** to the buffer.

There are two clipping styles, and we use both where each fits:

- **Per-pixel guard** — the simplest: `set_pixel` checks bounds and silently drops
  out-of-range writes. Used by `draw_line`, `draw_rect`, `draw_char` (their pixels
  are scattered, so a per-pixel check is natural).

  ```cpp
  void set_pixel(int x,int y,Color c){
      if (x<0||y<0||x>=width||y>=height) return;   // <-- the safety net
      fb_.pixels[y*fb_.pitch + x] = c;
  }
  ```

- **Pre-clip the region** — for big solid areas it's wasteful to bounds-check
  every pixel. `fill_rect` instead clamps the rectangle's edges to the buffer
  *once*, then fills the trimmed span with no inner checks:

  ```cpp
  int x0=max(0,x), y0=max(0,y);
  int x1=min(width, x+w), y1=min(height, y+h);
  for (yy in [y0,y1)) for (xx in [x0,x1)) row[xx]=c;   // no per-pixel branch
  ```

> ASan (Chapter 01, `-DENGINE_SANITIZE=ON`) is your friend here: if a clip is ever
> wrong, an out-of-bounds write becomes an immediate, located error instead of
> mysterious corruption.

---

## 3. Color and alpha blending

`color.hpp` treats a pixel as `0xAARRGGBB` and gives `rgba()/rgb()` to build colors
and `a_of/r_of/g_of/b_of` to take them apart. The interesting function is
**`blend`** — "source over destination", the standard way to draw something
semi-transparent on top of what's already there:

```
   out = src*alpha + dst*(1 - alpha)         (per channel; alpha in 0..1)
```

In 8-bit integers with rounding:

```cpp
out = (src*sa + dst*(255-sa) + 127) / 255;   // per channel, sa = source alpha 0..255
```

Worked example: drawing 50%-alpha red (`sa=128`) over blue:
`R = (255*128 + 0*127 + 127)/255 ≈ 128`, `B = (0*128 + 255*127 + 127)/255 ≈ 127`
→ a purple `(128, 0, 127)`. Exactly the half-and-half you'd expect. We special-case
`sa==255` (opaque → copy) and `sa==0` (invisible → skip) for speed.

> Detail: we keep the framebuffer **opaque** (result alpha `255`). It's a screen,
> not a layer to be composited again. If you ever stack translucent *buffers*,
> you'd want "premultiplied alpha" — out of scope here, noted for later.

---

## 4. Lines: Bresenham's algorithm

Drawing a line means choosing which pixels best approximate the ideal line. The
naive way (`y = mx + b` with floats) works but uses floating point and can leave
gaps on steep lines. **Bresenham's algorithm** is the classic: integer-only, one
pixel per step, no gaps, all directions.

The idea: walk along the major axis one pixel at a time, keeping an integer
**error** term that accumulates the line's slope. When the error crosses a
threshold, step in the minor axis too. Our form handles all 8 octants uniformly:

```cpp
int dx =  abs(x1-x0), sx = x0<x1 ? 1 : -1;
int dy = -abs(y1-y0), sy = y0<y1 ? 1 : -1;
int err = dx + dy;
for(;;){
    set_pixel(x0,y0,c);
    if (x0==x1 && y0==y1) break;
    int e2 = 2*err;
    if (e2 >= dy){ err += dy; x0 += sx; }   // step x
    if (e2 <= dx){ err += dx; y0 += sy; }   // step y
}
```

`sx/sy` carry direction (so it works right-to-left and bottom-to-top); `dy` is kept
negative so the two comparisons share one `err`. You don't need to memorize the
derivation — but notice there's *no floating point and no division per pixel*,
which is exactly why it's fast enough to call thousands of times a frame.

---

## 5. Rectangles

- **`fill_rect`** — solid block, pre-clipped (see §2). The workhorse for
  backgrounds, the chess board's squares (M1), UI panels.
- **`draw_rect`** — a 1-pixel outline: top and bottom rows, left and right columns,
  using the per-pixel `set_pixel` (so it clips for free). Used for highlights, like
  the legal-move squares in chess.

---

## 6. Sprites (with alpha)

A **`Sprite`** is just a pointer to ARGB pixels plus width/height. `blit` copies it
to the framebuffer at `(x,y)`, clipping each row/column and **alpha-blending** each
pixel (skipping fully transparent ones so sprites can have shaped, non-rectangular
silhouettes):

```cpp
Color src = s.pixels[sy*s.w + sx];
if (a_of(src) == 0) continue;             // transparent → leave background
dst = blend(dst, src);                    // else composite over what's there
```

The demo builds a 16×16 sprite *procedurally* — an amber diamond whose alpha fades
toward the edges — precisely so you can watch the blend soften it against the
background as it orbits.

---

## 7. Bitmap text

We embedded a public-domain **8×8 font** (`font8x8.hpp`, vendored in
`font8x8.inc`). Each character is 8 bytes — one per row — and within a row byte
**bit 0 is the left-most pixel**:

```
   'A' row 0 = 0x0C = 0b00001100      column: 76543210
                                              ....##..   -> apex of the A
```

`draw_char` loops the 8×8 grid and plots a pixel wherever the glyph bit is set;
`scale` draws each font pixel as a `scale×scale` block (so the FPS counter and a
big title can share one font). `draw_text` walks the string, advancing 8·scale
pixels per glyph and handling `\n`:

```cpp
for (col 0..7) if (glyph[row] & (1u<<col))  plot (x+col, y+row);  // bit0 = left
```

That's the entire text system — no font files, no shaping, perfect for a
hand-built engine and trivially portable to the web.

---

## 8. Wiring into the frame

`Context` now carries a `gfx::Renderer2D& gfx` (it replaced the raw framebuffer).
`App::frame` builds one per frame over the platform framebuffer and passes it:

```cpp
gfx::Renderer2D renderer(platform::framebuffer());
Context ctx{ renderer, dt, time_, accumulator_/kFixedDt };
scene_->render(ctx);
```

Scenes now draw with a clean API (`g.clear(...)`, `g.draw_line(...)`, …) and never
touch raw pixels unless they want to.

---

## 9. Run & observe

```sh
cmake --build build
./build/demo
```

You should see, on a dark-navy background: a **rotating fan of blue lines** from
the center, a **red filled rectangle with a white outline** top-left, an **amber
diamond sprite orbiting** the center (its edges softly blending), and **two lines
of text** at the bottom (one large, one small). Everything there was drawn by the
code in this chapter.

```sh
HAND_ENGINE_FRAMES=60 ./build/demo   # head-less smoke test, exits 0
```

---

## 10. Common pitfalls

- **Skipping clipping** → out-of-bounds writes / crashes. Every primitive clips.
- **Forgetting alpha = 0xFF** → "invisible" opaque drawing (Chapter 02's trap).
- **Using `width` instead of `pitch`** for the row stride → skewed image.
- **Per-pixel work in tight loops** → slow. Pre-clip big fills; keep the inner
  loop branch-free where you can.
- **Signed index overflow** at extreme coordinates → clip *before* computing
  `y*pitch+x`.

---

## 11. Glossary

- **Clipping** — discarding draw operations outside the buffer.
- **Source-over / alpha blend** — compositing translucent `src` onto `dst`.
- **Bresenham** — integer line-drawing via an error term.
- **Blit** — copy a sprite's pixels into the framebuffer.
- **Bitmap font** — glyphs stored as pixel bitmaps (vs vector/TrueType).
- **Advance** — how far the cursor moves after drawing a glyph.

---

## 12. Exercises

1. **Draw a checkerboard.** Use `fill_rect` in a double loop to draw an 8×8 grid
   of alternating squares — you've just prototyped the chess board (M1). *(Hint:
   `(row+col) % 2` picks the color.)*
2. **Change sprite alpha.** In `RendererTestScene`, make the diamond fully opaque
   (`a = d<8 ? 255 : 0`) and compare — the soft edge disappears. Why?
3. **A circle outline.** Draw a circle with `set_pixel` using
   `x=cx+r*cos(t), y=cy+r*sin(t)` for `t` in small steps. Then think about why
   Bresenham-style integer circle algorithms exist. *(Hint: floats + gaps.)*
4. **Clip torture test.** Call `g.draw_line(-100,-50, width+80, height+30, ...)`.
   It should draw the on-screen part and not crash. Build with
   `-DENGINE_SANITIZE=ON` to be sure no out-of-bounds slips through.

---

## 13. What's next

We can draw, but the demo ignores us. **Chapter 06** adds **normalized input** —
keyboard (pressed / released / held) and mouse — read through the engine, never
SDL, so a scene can finally respond to the player.
