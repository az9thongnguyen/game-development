# Chapter 69 — Anti-aliasing I: SSAA and the supersample seam

> **Where we are.** Ch.68 gave us anti-aliased *text*. But every other primitive —
> lines, rectangles, the 3D raycaster — still point-samples one value per pixel, so
> edges stair-step ("răng cưa"). This chapter adds **supersampling (SSAA)**: one
> seam that softens *everything the renderer draws*, including the 3D scenes, by
> rendering into a larger buffer and shrinking it on the way to the screen.

---

## 1. Why edges alias

A software renderer decides each pixel's colour by asking a yes/no question at a
single point: *is this pixel inside the shape?* That is **point sampling**. Along a
slanted edge the answer flips abruptly from "in" to "out", so a diagonal becomes a
staircase of hard steps. There is no pixel that is "40% inside", even though
visually the edge cuts through it.

The cure is to **sample more finely than the screen** and average. If a pixel is
covered by the shape in 4 of its 9 sub-samples, it should be ~44% ink. Averaging
turns the hard in/out boundary into a smooth ramp of intermediate shades, and the
eye reads that ramp as a clean edge.

---

## 2. SSAA: render big, shrink down

**Supersampled anti-aliasing** is the most brute-force, most universal way to do
that averaging:

1. Allocate the framebuffer at **N× the logical size** in each axis (we use N=2 →
   4× the pixels).
2. Render the whole scene into it normally. Every primitive now has N× the
   resolution to place its edges.
3. On present, **downsample** the big buffer to the window. A 2×2 box of physical
   pixels averages into one screen pixel — that average *is* the anti-aliasing.

```
   render at 2× (physical)          downsample 2×2 → 1        on screen
   ┌─┬─┬─┬─┬─┬─┬─┬─┐                ┌───┬───┬───┬───┐
   │█│█│ │ │ │ │ │ │   each 2×2     │ █ │ ▓ │ . │ . │   the diagonal's
   │█│█│█│ │ │ │ │ │   block  →     │ ▓ │ █ │ ▓ │ . │   step edges become
   │ │█│█│█│ │ │ │ │   averaged     │ . │ ▓ │ █ │ ▓ │   grey — smooth
   │ │ │█│█│█│ │ │ │                │ . │ . │ ▓ │ █ │
```

The beauty is that **no primitive has to know**. Lines, filled rects, sprites, and
— crucially — the **3D raycaster** all just draw into a bigger buffer and come out
anti-aliased. The raycaster in particular gets AA "for free": it already loops over
`framebuffer().width × height`, so a 2× buffer means it casts 4× the rays and the
downsample averages them. (That 4× is also the cost — see §5.)

---

## 3. The seam: three moving parts, one invariant

The invariant we protect: **game code keeps writing in logical coordinates and
never learns SSAA exists.** Three pieces cooperate (see `backend_sdl.cpp`,
`renderer2d.cpp`):

**(a) The platform allocates a physical buffer.**
```cpp
g_ss    = clamp(cfg.supersample, 1, 4);
g_log_w = cfg.fb_width;  g_log_h = cfg.fb_height;      // logical (what the game sees)
g_fb_w  = g_log_w * g_ss; g_fb_h = g_log_h * g_ss;     // physical (what we rasterize)
// texture + pixel buffer are PHYSICAL; the window stays logical*scale
SDL_RenderSetLogicalSize(renderer, g_log_w, g_log_h);  // present target = logical
```
On present, `SDL_RenderCopy(texture_physical → logical rect)` with **linear**
filtering shrinks the physical texture to logical size — the GPU does the box-ish
average. (SSAA therefore *requires* linear + non-integer scaling; nearest would
just pick one of the four samples and defeat the point.)

**(b) `Renderer2D` scales logical → physical.** Every primitive multiplies its
inputs by `ss_` and writes through the physical sinks; `width()/height()` divide by
`ss_` so the game still reads the logical size:
```cpp
void set_pixel(int x,int y,Color c){ fill_phys(x*ss_, y*ss_, ss_, ss_, c); } // 1 logical px = ss×ss
int  width() const { return fb_.width / ss_; }                                // logical
```
`ss_==1` makes all of this an identity — the renderer is byte-for-byte its old
self, which is why turning SSAA off is perfectly safe.

**(c) The mouse stays logical.** The backend maps window coordinates to the
**logical** size, not the physical one, so UI hit-testing (widget rects in logical
space) still lines up with the cursor:
```cpp
g_input.mouse_x = mx * g_log_w / window_w;   // NOT g_fb_w
```
Getting this wrong is the classic SSAA bug: everything draws fine but clicks land
at half/double the expected place.

---

## 4. A worked example

Logical scene 960×600, `supersample = 2`, window scale 1:

```
physical framebuffer = 1920 × 1200  = 2 304 000 px   (4× the logical 576 000)
game draws a panel at logical (100,80,300,120)
  → physically filled at (200,160,600,240)
present: RenderCopy 1920×1200 texture → 960×600 logical rect (linear)
  → each screen pixel = average of a 2×2 physical block  → anti-aliased
mouse at window (480,300) → logical (480,300)  (unchanged; matches widget rects)
```

---

## 5. Cost, and why it's a toggle

SSAA's honesty is its price: **N² more pixels to fill.** At N=2 the whole software
renderer does 4× the per-pixel work. For 2D scenes (a few thousand filled pixels)
this is nothing. For the **3D raycaster**, which already touches every pixel every
frame, 4× can hurt the frame budget. So `supersample` is a per-scene knob in
`main.cpp`: 2D scenes run `ss=2` freely; heavy 3D scenes can stay at `ss=1` and
lean on the per-primitive AA (next chapter) for their 2D HUD overlay.

```
// ponytail: SSAA is the universal hammer; it is also the expensive one.
// Per-primitive AA (Ch.70) covers 2D crisply at ss=1 — use SSAA where the
// win (3D edges, everything-at-once) is worth the 4× fill.
```

---

## 6. SSAA vs. per-primitive AA — why we have both

They are complementary, not redundant:

| | SSAA | Per-primitive AA (Ch.70) |
|---|---|---|
| Covers | **everything**, incl. 3D | only shapes you call the AA version of |
| Cost | N² fill, always | ~free, only on drawn edges |
| Quality on 2D UI | good | often crisper (exact analytic coverage) |
| Works at ss=1 | n/a | yes |

The design uses per-primitive AA for the 2D UI (crisp at any setting) and SSAA as
the global softener for scenes that want it — especially 3D, where analytic
coverage isn't available.

---

## 7. Pitfalls

- **Mouse in the wrong space.** Map input to *logical*, or every click is offset by
  the SSAA factor.
- **Nearest filtering on downsample.** SSAA needs *linear* (averaging); nearest
  throws away the extra samples.
- **Integer scale on.** `SDL_RenderSetIntegerScale(true)` forbids the fractional
  shrink SSAA relies on — turn it off when `ss>1`.
- **Forgetting to divide `width()/height()`.** If these report physical size,
  centring math (`width()/2`) drifts and layouts break.
- **Assuming SSAA fixes text letterforms.** It softens edges but an 8×8 bitmap is
  still crude — that's why we did a real font first (Ch.68). Text is rasterized at
  `font_px * ss` so it's genuinely sharp, not just shrunk.

---

## 8. Glossary

- **Aliasing** — artifacts (stair-steps) from sampling a signal too coarsely.
- **Point sampling** — one sample per pixel; the source of edge aliasing.
- **SSAA (supersampling)** — render at N× resolution, average down to the screen.
- **Downsample** — shrink a high-res image by averaging blocks of pixels.
- **Logical vs physical** — the coordinate size the game uses vs. the enlarged
  buffer actually rasterized.
- **Box filter** — averaging an N×N block equally (what a 2× linear shrink
  approximates).

---

## 9. Exercises

1. **Measure the cost.** Run the raycaster at `ss=1` and `ss=2`; log frame time.
   Where's the crossover where 4× fill stops being worth it?
2. **True box downsample.** SDL's linear shrink is bilinear, not an exact 2×2 box.
   Write a CPU downsample that averages each 2×2 block and compare edge quality.
3. **Per-axis / 1.5× SSAA.** Support non-integer factors (e.g. 1.5×). What breaks in
   the "1 logical px = ss×ss block" assumption, and how would you fix it?
4. **Adaptive SSAA.** Only supersample tiles that contain edges (detect via a
   cheap variance pass). Sketch the bookkeeping.
5. **Gamma.** Averaging in sRGB darkens edges slightly. Where would you convert to
   linear before the downsample, and back after?

---

*Next: [Chapter 70 — Anti-aliasing II: Xiaolin Wu lines & coverage rasterization](70-antialiasing-wu-coverage.md),
the analytic, near-free AA we use for the 2D UI shapes.*
