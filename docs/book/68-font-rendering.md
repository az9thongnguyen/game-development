# Chapter 68 — Font rendering: from an 8×8 bitmap to anti-aliased glyphs

> **Where we are.** For 67 chapters our text was the embedded 8×8 bitmap font
> (`font8x8.hpp`): one byte per row, one bit per pixel, drawn as solid blocks and
> scaled by *replicating* those blocks. It is honest and dependency-free — and it
> is the single most visible source of the "vỡ pixel" (broken-pixel) look. This
> chapter replaces it with **real, scalable, anti-aliased glyphs** rasterized from
> a TrueType font by `stb_truetype`, and explains every idea underneath.

---

## 1. Why the bitmap font looks broken

A bitmap font stores each glyph as a fixed grid of on/off pixels. Ours is 8×8:

```
  'A' in font8x8 (1 = lit)          scaled ×3 (block replication)
  . . X X . . . .                   . . . X X X X X X . . . . . .
  . X . . X . . .                   . . . X X X X X X . . . . . .
  X . . . . X . .                   . . . X X X X X X . . . . . .
  X . . . . X . .        →          X X X . . . . . . X X X . . .
  X X X X X X . .                    (each source pixel becomes a
  X . . . . X . .                     3×3 solid square — the edges
  X . . . . X . .                     stay perfectly hard)
  . . . . . . . .
```

Two problems, both structural:

1. **No anti-aliasing.** Every pixel is fully on or fully off. A diagonal stroke
   becomes a staircase; there are no in-between shades to fool the eye into seeing
   a smooth edge. Scaling up makes the staircase *bigger*, not softer.
2. **It does not scale.** The glyph has only 8×8 = 64 cells of detail. Blowing it
   up ×3 gives big pixels, not more letterform. There is no "16px" or "24px"
   version — only the one grid, magnified.

The fix is to stop storing *pixels* and start storing the *shape*.

---

## 2. Outlines, not pixels

A TrueType (`.ttf`) font stores each glyph as a **vector outline**: a set of
closed contours made of straight and quadratic-Bézier segments, in an abstract
"em" coordinate space (typically 1000 or 2048 units per em, independent of any
pixel size). The letter *is* the region enclosed by those contours.

To put that on screen at a given pixel height you:

1. **Scale** the outline from font units to pixels.
2. **Rasterize** it — for every pixel, compute how much of the pixel the shape
   covers (0.0 = outside, 1.0 = fully inside, values between = on the edge).
3. **Composite** that coverage as the glyph's alpha.

Step 2 is where anti-aliasing *comes from for free*: a pixel the edge slices
through gets partial coverage, e.g. 0.4, and is drawn as 40%-opacity ink. The eye
integrates those grey edge pixels into a smooth line. Same idea as SSAA
(next chapter), but computed analytically per glyph instead of by brute-force
supersampling.

```
  outline of 'A' scaled to 16px        coverage after rasterizing
     /\                                 . . ▁ ▓ ▓ ▁ . .      (▁ = ~0.3
    /  \                                . ▁ ▓ █ █ ▓ ▁ .       ▓ = ~0.7
   / __ \                               . ▓ █ ▓ ▓ █ ▓ .       █ = ~1.0)
  / /  \ \                              ▁ ▓ ▁ . . ▁ ▓ ▁      edge pixels
                                        the diagonal edges are GREY, not hard
```

Writing a robust outline rasterizer (contour winding, Bézier flattening, correct
coverage) is a chapter of its own — so we use **`stb_truetype.h`**, a single-file,
public-domain library that does exactly this. It fits the project's rule the same
way SDL does: a *thin, well-scoped* dependency at the edge, with all of it confined
to one translation unit (`font.cpp`) so the rest of the engine never sees it.

---

## 3. Glyph metrics: how text gets positioned

Rasterizing a glyph gives you a little coverage bitmap. Placing a *run* of them
correctly needs **metrics**. The vocabulary (all in pixels, after scaling):

```
      pen (origin)
        │
        ▼
  ──────┼───────────────────────  ← baseline
        │   ██████                   ascent  = top of line → baseline
        │  ██    ██                  descent = baseline → bottom of line (negative)
        │  ████████   ← glyph        advance = pen → next pen (includes side gaps)
        │  ██    ██                  bearing_x = pen → left edge of the glyph box
        │◄────────►                  top     = baseline → top row of the glyph box
         advance                             (usually negative: the box starts above)
```

- **baseline** — the invisible line letters sit on. Positioning is relative to it.
- **advance** — how far to move the pen after drawing a glyph. Proportional fonts
  give `i` a small advance and `W` a large one — that alone kills the "monospace
  8px everywhere" feel.
- **bearing_x / top** — where the glyph's coverage box sits relative to the pen and
  baseline. `top` is normally *negative* in stb's convention (y grows downward, and
  the box begins above the baseline).
- **ascent / descent / line_gap** — per-face vertical metrics; the recommended
  line-to-line step is `ascent − descent + line_gap` (descent is negative, so this
  is a sum of magnitudes).

Our `draw_text` treats the caller's `y` as the **top of the line** (to stay
compatible with the old bitmap API), so it computes the baseline once as
`y + ascent`, then for each glyph draws its box at
`(pen + bearing_x, baseline + top)` and advances `pen += advance`.

---

## 4. The atlas: rasterize once, reuse every frame

Rasterizing outlines is not free, and a HUD redraws the same characters 60×/second.
So we rasterize each size **once** into an **atlas** and cache it:

- The first time a `(face, pixel-size)` pair is used, we rasterize printable ASCII
  32–126 (95 glyphs) into per-glyph 8-bit coverage buffers and store their metrics.
- Every later draw at that size is just a memory read + a blend — no rasterizing.

The cache key is the **pixel size**, and this is deliberate: it bounds cardinality.
If we cached per *string* or per *scale factor* the map would grow without limit
(the same footgun as route metrics in Ch.67). A handful of sizes from the type
scale (12/14/16/20/28) → a handful of atlases → a few kilobytes each. ASCII-only
keeps it small; Unicode would need a glyph-on-demand cache (an exercise).

```
  Font
   ├─ ttf bytes (kept alive — stb points INTO them)
   ├─ stbtt_fontinfo (parsed tables)
   └─ sizes: { 14 → Atlas14, 20 → Atlas20, ... }   ← built lazily
                         │
                         └─ glyph[95]: {w,h,advance,bearing_x,top, cov→[w*h bytes]}
```

---

## 5. Code walkthrough

### 5.1 Loading (pure core, no I/O)

`text::Font` knows nothing about files. The caller reads the `.ttf` bytes through
the **asset seam** (so the web VFS works, Ch.32) and hands them over:

```cpp
auto bytes = assets::load_file("fonts/Inter.ttf");   // std::optional<vector<uint8_t>>
auto font  = text::Font::load_from_bytes(std::move(*bytes));   // nullptr on parse fail
```

Inside, the bytes are **moved into the Font and kept alive**, because
`stbtt_InitFont` stores a *pointer into* them rather than copying — a classic
lifetime trap (see Pitfalls):

```cpp
f->p_->ttf = std::move(ttf);                                  // owns the bytes
int off = stbtt_GetFontOffsetForIndex(f->p_->ttf.data(), 0);  // first face in the file
if (off < 0 || !stbtt_InitFont(&f->p_->info, f->p_->ttf.data(), off)) return nullptr;
```

> **Variable fonts.** Inter ships as a *variable* font (one file, many weights).
> stb_truetype reads the file's **default instance** (Regular), which is exactly
> what we want for body UI. Bold is faux-synthesized later (draw twice, +1px) or a
> separate static face — an easy extension.

### 5.2 Building a size atlas

```cpp
s->scale = stbtt_ScaleForPixelHeight(&info, px);        // font units → px factor
stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);        // in font units...
s->ascent = lround(asc * s->scale);                     // ...scaled to px

for (cp = 32..126) {
    stbtt_GetCodepointHMetrics(&info, cp, &adv, &lsb);          // advance, side bearing
    stbtt_GetCodepointBitmapBox(&info, cp, scale,scale, &x0,&y0,&x1,&y1); // px box
    w = x1-x0; h = y1-y0;
    glyph.advance = lround(adv * scale);
    glyph.bearing_x = x0;  glyph.top = y0;
    if (w>0 && h>0) {
        cov.resize(w*h);
        stbtt_MakeCodepointBitmap(&info, cov.data(), w,h, w /*stride*/, scale,scale, cp);
        glyph.cov = cov.data();     // 8-bit coverage, 0..255
    }                                // blank glyphs (space) keep cov = nullptr
}
```

`stbtt_MakeCodepointBitmap` is the rasterizer: it writes an 8-bit **coverage**
value per pixel — precisely the anti-aliasing from §2.

### 5.3 Drawing = coverage → alpha

The renderer folds each coverage byte into the ink colour's alpha and blends:

```cpp
// Renderer2D::blend_pixel(x, y, colour, coverage)
uint32_t a = a_of(colour) * coverage / 255;        // combine glyph coverage with ink alpha
blend_pixel(x, y, (colour & 0x00FFFFFF) | (a<<24)); // source-over onto the framebuffer
```

`draw_text` walks the string, places each glyph box relative to the baseline, and
calls that per covered pixel. White text on black therefore produces **grey edge
pixels** — the test asserts exactly this (intermediate shades exist), which is the
machine-checkable definition of "it's anti-aliased".

### 5.4 The fallback

If no font is set (headless tests, or the `.ttf` failed to load), `draw_text`
delegates to the old 8×8 path. The engine **never crashes for lack of a font** and
old code keeps working — the new capability is purely additive.

---

## 6. A worked number

Inter reports `unitsPerEm = 2048`, `ascent = 1984`, `descent = −494` font units.
Ask for **20px**:

```
scale   = ScaleForPixelHeight(20) = 20 / (ascent − descent)          (stb's convention)
        = 20 / (1984 − (−494)) = 20 / 2478 ≈ 0.008071 px per font-unit
ascentPx  = round(1984 × 0.008071) ≈ 16 px
descentPx = round(−494 × 0.008071) ≈ −4 px
lineStep  = ascent − descent + gap = 16 − (−4) + gap ≈ 20 + gap px
```

So a 20px face is ~16px above the baseline and ~4px below — and the whole run
positions off that single baseline computed as `y + 16`.

---

## 7. Pitfalls

- **Dangling font data.** `stbtt_InitFont` keeps a *pointer* into your `.ttf`
  buffer. If that buffer is a local that goes out of scope (or a `vector` that
  reallocates), every later call reads freed memory. We move the bytes into the
  Font and never touch them again → stable for the Font's lifetime.
- **Baseline vs. top.** Mixing up "y is the top of the line" with "y is the
  baseline" shifts all text by ~one ascent. Pick one convention and convert once.
  We keep the old *top* convention and add `ascent` internally.
- **Cache cardinality.** Cache per *size*, never per string or per arbitrary
  scale, or the atlas map grows unbounded.
- **Coverage ≠ colour.** Coverage is a *mask*; multiply it into the ink's alpha,
  don't write it as a grey pixel — otherwise coloured text loses its colour.
- **Pointer stability in the cache.** `glyph.cov` points into a `vector` owned by a
  heap-allocated `Size`; store the `Size` behind a `unique_ptr` so moving it into
  the map doesn't invalidate those pointers.
- **Tiny sizes look soft.** stb does anti-aliasing but no *hinting*; below ~11px
  outlines get blurry. Oversampling (stb supports it) or a hinted rasterizer
  (FreeType) is the upgrade — noted, not built.

---

## 8. Glossary

- **Glyph** — the drawn shape of a character at a given size.
- **Outline** — the vector contours (lines + Béziers) defining a glyph in font
  units.
- **Rasterize** — convert an outline to a pixel coverage bitmap.
- **Coverage** — fraction of a pixel the shape covers (the source of AA), stored
  0–255.
- **Baseline** — the line glyphs sit on; positioning reference.
- **Advance** — horizontal pen movement after a glyph (proportional per glyph).
- **Bearing** — offset from the pen/baseline to the glyph's coverage box.
- **Ascent / descent / line gap** — per-face vertical metrics; line step =
  ascent − descent + gap.
- **Atlas** — a cached set of rasterized glyphs (+ metrics) for one size.
- **Hinting** — grid-fitting an outline for crispness at small sizes (not done
  here).

---

## 9. Exercises

1. **Faux bold.** Add a `bold` flag to `draw_text` that draws each glyph twice,
   the second time offset by 1px in x. Where does it look good, where does it fall
   apart, and why is a real Bold face better?
2. **Kerning.** stb exposes `stbtt_GetCodepointKernAdvance`. Add kerning to
   `text_width` and `draw_text`. Measure the width difference on `"AVATAR"`.
3. **Right-align / center.** Using `text_width`, add `draw_text_centered(cx, y,…)`.
   Why is a cached `text_width` (per size) cheap enough to call every frame?
4. **Glyph-on-demand.** Replace the "rasterize all 95 ASCII up-front" with a
   lazy per-codepoint cache so non-ASCII/Unicode can be added. What's the tradeoff
   vs. batch rasterizing?
5. **Oversampling.** Rasterize at 2× or 3× oversample (stb's oversample API) and
   downsample; compare small-size sharpness against the naïve version.

---

*Next: [Chapter 69 — Anti-aliasing I: SSAA and the supersample seam](69-antialiasing-ssaa.md),
which softens **everything** the renderer draws — lines, shapes, even the 3D
raycaster — with one seam, complementing the analytic AA we just got for text.*
