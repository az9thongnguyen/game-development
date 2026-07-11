# Chapter 74 — Seamless tiling on a torus

A wall texture is small — 128×128 — but the wall is huge. The engine draws the wall
by **repeating** the texture across it, like tiles on a floor. If the texture's right
edge doesn't line up with its own left edge, you get a hard line at every repeat:

```
non-seamless (edges clash):        seamless (edges continuous):
 ▓▒░│▓▒░│▓▒░│▓▒░                     ▓▒░░▒▓▓▒░░▒▓▓▒░
 ░▒▓│░▒▓│░▒▓│░▒▓                     ░▒▓▓▒░░▒▓▓▒░░▒▓
     ^   ^   ^  seams                  (no seam anywhere)
```

Making a texture *tileable* — so it repeats with no seam — is the difference between
a usable asset and a broken one. This chapter shows the clean way to do it, and
corrects a tempting but **wrong** way to test it.

---

## 1. The wrong fixes

Two "obvious" ideas that disappoint:

- **Mirror the texture** (flip every other tile). Kills the hard seam, but creates
  obvious mirror symmetry — the eye spots the repeated reflection instantly.
- **Blur the edges together** after generating. Softens the seam but smears detail
  into a visible band, and you must re-blur after every parameter change.

Both patch the *symptom*. The right fix removes the seam at the *source*: generate
noise that is **already periodic**.

---

## 2. The torus idea

Recall from chapter 73 that noise is defined on an integer **lattice**. A texture is
seamless if the noise **repeats with a period** equal to the tile. Think of gluing
the left edge to the right and the top to the bottom — the flat texture becomes the
surface of a **torus** (a donut). On a torus there is no edge, so there can be no
seam.

```
   flat tile              glue L↔R, T↔B            torus (no edges)
  ┌─────────┐             ┌─────────┐                 .-""""-.
  │         │   ───▶      │ ⇄     ⇄ │    ───▶        /  _   _  \
  │         │             │ ⇵     ⇵ │               |  (_) (_)  |
  └─────────┘             └─────────┘                 \  ___  /
                                                        `-...-'
```

Concretely: when we look up a lattice corner, we **wrap its coordinate modulo the
period** before hashing. That single line makes corner `period` identical to corner
`0`, so the pattern loops:

```cpp
int wrap(int a, int p) { int m = a % p; return m < 0 ? m + p : m; }   // 0..p-1
...
const int x0 = wrap(ix, period), x1 = wrap(ix + 1, period);
const int y0 = wrap(iy, period), y1 = wrap(iy + 1, period);
```

Because the generator samples `u, v` over `[0, 1)` and sets `period = frequency`, the
lattice coordinate `u·period` runs from `0` to `period` across the tile. `wrap`
sends `period` back to `0` — so the noise at `u = 0` and the noise at `u = 1` are the
*same value*. The tile's opposite edges meet by construction.

---

## 3. The right way to *test* it (and why the obvious test is wrong)

It is tempting to test seamlessness by comparing pixels: "column 0 should equal the
last column." **That is wrong.** The last rendered column is at `u = (w−1)/w`, not at
`u = 1`. Column 0 (`u = 0`) is meant to sit next to the *next tile's* column 0 — which
is `u = 1` in this tile's coordinates. The seam is between this tile's last column and
the next tile's first column, and those two are one texel apart, *not* equal.

The correct invariant is a property of the **noise function**, not the pixels:

> A texture is seamless ⟺ its noise is **periodic with period 1**:
> `noise(u, v) == noise(u + 1, v)` and `noise(u, v) == noise(u, v + 1)`.

That is exactly what the torus wrap guarantees, and it is what
[`tests/test_studio.cpp`](../../tests/test_studio.cpp) checks:

```cpp
CHECK(approx(value_noise(0.3, 0.4, 4, 7),  value_noise(1.3, 0.4, 4, 7)));   // +1 in u
CHECK(approx(perlin_noise(0.3, 0.4, 4, 7), perlin_noise(1.3, 0.4, 4, 7)));
CHECK(approx(fbm(0.3, 0.4, Basis::Perlin, 4, 4, 0.5, 2.0, 7),
             fbm(1.3, 0.4, Basis::Perlin, 4, 4, 0.5, 2.0, 7)));
```

At `u = 0.3` versus `u = 1.3`, the fractional part `fx` is identical and the integer
part differs by exactly `period`, which `wrap` erases — so the values are equal to
the last floating-point bit. The test asserts periodicity, so it can never be fooled
by a "close enough" seam.

---

## 4. fBm and tiling: keep the frequencies integer

fBm sums octaves at `freq`, `freq·lacunarity`, `freq·lacunarity²`, … For *every*
octave to tile, *every* octave's frequency must be an integer that divides the tile
evenly. Two rules follow:

- **`base_freq` must be an integer** (it is — the UI slider is whole-numbered).
- **`lacunarity` should be an integer** (2.0 is the default). With `lacunarity = 2`,
  the octave frequencies are `4, 8, 16, …` — all integers, all tileable. A fractional
  lacunarity like `2.5` makes `freq = 10, 25, …` after rounding, and the rounding
  breaks exact periodicity → a faint seam creeps back.

The Wood and Checker bases inherit the same requirement:

- **Checker** tiles only when `frequency` is **even** (an odd count leaves two same-
  colour squares adjacent across the seam).
- **Wood** rings use `sin((u + n)·frequency·2π)`; the `·frequency·2π` makes the phase
  advance by a whole number of `2π` per unit `u`, so `u` and `u+1` land on the same
  phase — seamless, provided `n` (the fBm term) is seamless too.

---

## Pitfalls

- **Testing edge-pixel equality.** The classic mistake (see §3). Test *function*
  periodicity, not pixel columns.
- **Non-integer frequency or lacunarity.** Silently reintroduces a seam. If a texture
  "almost tiles", check these first.
- **Wrapping in pixel space instead of lattice space.** You must wrap the *lattice
  coordinate* (`ix mod period`), not the pixel index. Wrapping pixels does nothing.
- **Odd checker frequency.** Produces a visible mismatch on two of the four edges.

## Glossary

- **Tileable / seamless** — repeats with no visible discontinuity at tile borders.
- **Period** — the distance in `uv` over which the noise repeats (here, 1.0).
- **Torus wrap** — reducing lattice coordinates modulo the period so the pattern loops.

## Exercises

1. **Break it on purpose.** Set `lacunarity = 2.5` and tile the preview 2×2. Where does
   the seam appear, and why only in the high-frequency detail? *(Hint: the low octaves
   still tile; only the rounded ones don't.)*
2. **Prove the corner case.** Add a test that `value_noise(0.0, v) == value_noise(1.0,
   v)` exactly. *(Hint: at u=1 the lattice coord is `period`, which `wrap`s to 0.)*
3. **Rectangular tiles.** Generalize `wrap` to independent `period_x`, `period_y` for
   non-square textures. *(Hint: two periods, two wraps — nothing else changes.)*

Next: chapter 75 — wrapping the generator in an interactive tool with a live preview,
saving, and a collection.
