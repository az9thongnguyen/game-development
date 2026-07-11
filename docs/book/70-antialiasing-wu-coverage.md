# Chapter 70 — Anti-aliasing II: Xiaolin Wu lines & coverage rasterization

> **Where we are.** Ch.69's SSAA softens everything by brute force (4× fill).
> This chapter does the opposite: compute the *exact* fraction of each pixel a
> shape covers and blend that in — **analytic anti-aliasing**. It costs almost
> nothing (only edge pixels do extra work), stays crisp even at `ss=1`, and is what
> the 2D UI uses for smooth lines, rounded panels, and circular knobs.

---

## 1. Coverage is the whole idea

Every anti-aliasing method answers one question per pixel: *what fraction of me is
covered by the shape?* SSAA estimates it by counting sub-samples. Analytic AA
*calculates* it from geometry. Once you have a coverage `c ∈ [0,1]`, drawing is the
same everywhere — fold it into the ink's alpha and blend:

```cpp
// Renderer2D::blend_cov(x, y, colour, coverage)   — the shared AA sink
uint32_t a = a_of(colour) * coverage / 255;         // 0..255 coverage → scaled alpha
dst = source_over(dst, colour_with_alpha(a));
```

The font glyphs (Ch.68), the Wu lines, and the coverage shapes below all deposit
through this one function. The only difference between primitives is *how they
compute coverage*.

---

## 2. Xiaolin Wu's line

Bresenham picks the single nearest pixel at each step → a hard staircase. **Wu's
algorithm** instead lights the **two pixels straddling** the true line, splitting
one unit of ink between them by distance: the closer pixel gets more.

```
   true line passes at y = 3.7 for this column
   ┌──────┐
   │ y=3  │  coverage = 1 − 0.7 = 0.3   (further)   ← two pixels share the
   ├──────┤                                            ink; their coverages
   │ y=4  │  coverage =     0.7         (closer)       sum to 1.0
   └──────┘
```

The bookkeeping (see `draw_line_aa`):

- **Orient once.** If the line is steeper than 45°, swap x/y so we always march the
  major axis; if it runs right-to-left, swap the endpoints. One code path handles
  all eight octants.
- **Gradient.** `grad = dy/dx` — how much the minor coordinate rises per major step.
- **March.** Keep a fractional minor coordinate `intery`. At each major step light
  `floor(intery)` with coverage `rfpart(intery) = 1−frac` and `floor(intery)+1` with
  `fpart(intery) = frac`, then `intery += grad`.
- **Endpoints.** The first/last columns are only partially inside the line's length,
  so their coverage is additionally scaled by an `xgap` term.

Because we anti-alias analytically, a horizontal or vertical line comes out
perfectly crisp (all the ink lands on one row/column, `frac = 0`), while a slanted
line gets exactly the grey it needs. We rasterize in **physical** space (endpoints
× `ss`), so Wu and SSAA compose: the analytic coverage is computed at the higher
resolution and then averaged again on downsample — belt and suspenders.

---

## 3. Coverage for filled shapes: a tiny distance field

For solid shapes we use a neat trick from signed-distance-field rendering. For a
pixel centre `p` and a shape, if `d` is the distance from `p` to the shape's edge
(negative inside), then

```
coverage ≈ clamp(0.5 − d, 0, 1)
```

i.e. a pixel whose centre is ≥0.5px inside is full, ≥0.5px outside is empty, and the
1px band across the edge ramps linearly — exactly one pixel of anti-aliasing.

### Rounded rectangle

A rounded rect is "all points within radius `r` of an inner **core rectangle**"
(the rect shrunk by `r` on every side). The distance from a pixel to that shape is
`dist(p, nearest point on core rect) − r`. Plug into the formula:

```
coverage = clamp(r + 0.5 − dist(p, coreRectCorner), 0, 1)
```

Rather than evaluate that for every pixel, `fill_round_rect` decomposes the shape
(cheap where coverage is trivially 1):

```
        pr
      ┌────┬──────────────┬────┐
   pr │ TL │   solid top   │ TR │   ← 4 corners: per-pixel arc coverage
      ├────┘   band        └────┤
      │      solid middle       │   ← one big fill_phys (coverage = 1)
      ├────┐   band        ┌────┤
      │ BL │  solid bottom │ BR │
      └────┴──────────────┴────┘
```

Only the four `pr × pr` corner boxes do the `sqrt`/coverage work; everything else is
a solid rectangle fill. For each corner pixel, `dist` is measured to that corner's
quarter-disc centre, giving a smooth arc.

### Circle and outlines

A filled circle is the same formula with a single centre:
`coverage = clamp(r + 0.5 − dist(p, centre), 0, 1)`.

An **outline** (ring) of thickness `t` is the difference of two filled coverages —
outer radius minus inner radius:

```
ring = clamp(r + 0.5 − d, 0, 1) − clamp((r − t) + 0.5 − d, 0, 1)
```

That yields a band that is anti-aliased on *both* edges. `draw_round_rect` and
`draw_circle` use it (the straight sides are thin `fill_phys` rectangles; only the
corners/arcs need the ring maths).

---

## 4. A worked number

`fill_round_rect(0,0, 10,10, radius=3)` at `ss=1`. Top-left corner disc centre is at
`(3,3)`. Pixel `(1,0)` has centre `(1.5, 0.5)`:

```
d = √((3−1.5)² + (3−0.5)²) = √(2.25 + 6.25) = √8.5 ≈ 2.92
coverage = clamp(3 + 0.5 − 2.92, 0, 1) = clamp(0.58) = 0.58  → ~148/255 grey
```

Its neighbour outward, `(0,0)` centre `(0.5,0.5)`:

```
d = √(2.5² + 2.5²) = √12.5 ≈ 3.54  → coverage = clamp(3.5 − 3.54) = 0  → empty
```

So along the diagonal the coverage falls 1.0 → 0.58 → 0 — a smooth rounded corner,
which is exactly what the unit test asserts (interior full, arc fractional, corner
empty, monotonic outward).

---

## 5. Pitfalls

- **Double-blending overlaps.** If two AA primitives cover the same pixel, their
  partial alphas composite twice and can darken the seam. Draw adjacent AA shapes so
  they meet on integer boundaries, or fill then stroke.
- **Off-by-half.** Coverage uses the pixel *centre* (`+0.5`). Dropping the half
  shifts every edge by half a pixel and biases the AA.
- **Gamma.** Blending coverage in sRGB slightly darkens thin strokes; correct AA
  blends in linear light (an exercise — we accept the small error).
- **`sqrt` per pixel.** Bounded to edge/corner pixels here, so it's cheap; a
  full-shape per-pixel loop (e.g. a naïve outline) is where it gets expensive.
- **radius > half the box.** Clamp `pr` to `min(w,h)/2`, or corners overlap and the
  discs fight.

---

## 6. Glossary

- **Coverage** — fraction of a pixel a shape occupies (0..1); the currency of AA.
- **Xiaolin Wu line** — a line algorithm that splits ink between the two pixels
  straddling the true line, weighted by distance.
- **Signed distance field (SDF)** — a function giving distance to a shape's edge
  (negative inside); `clamp(0.5 − d)` turns it into 1px AA coverage.
- **Ring / stroke** — an outline drawn as the difference of two filled coverages.
- **Analytic AA** — computing coverage from geometry rather than by supersampling.

---

## 7. Exercises

1. **Thicker Wu lines.** Extend `draw_line_aa` to a width `w` by stamping a small
   AA disc along the line. What's the cheap way vs. the correct way?
2. **Gamma-correct AA.** Convert to linear light before `blend_cov` and back after.
   Compare thin-stroke darkness against the current version.
3. **Anti-aliased polygon fill.** Generalize the coverage idea to an arbitrary
   convex polygon (hint: min distance to its edges, inside test by winding).
4. **Seam-free adjacency.** Draw two rounded rects sharing an edge without the
   double-blend darkening. Which primitive should own the shared pixels?
5. **Compare to SSAA.** Render the same rounded panel with per-primitive AA at
   `ss=1` and with SSAA at `ss=2`. Where does each look better, and why?

---

*This completes the anti-aliasing work: SSAA for everything-at-once (esp. 3D),
analytic coverage for crisp 2D UI, and real AA text — the "răng cưa / vỡ pixel"
complaint is addressed at the source. Next comes the design system (Ch.71) that
turns these sharp primitives into a UI that looks like one considered thing.*
