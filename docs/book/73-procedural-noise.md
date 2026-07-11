# Chapter 73 — Procedural noise: value, Perlin, and fBm

Every wall, floor, and terrain in a game needs a *texture*. You can download PNGs
— but then you ship megabytes of art, and you learn nothing. The alternative is to
**compute** textures from a formula. That is *procedural generation*, and its
foundation is **noise**: a function `noise(x, y)` that looks random but is smooth,
repeatable, and controllable.

This chapter builds three noise functions, each better than the last, in
[`src/games/studio/noise.cpp`](../../src/games/studio/noise.cpp). They are the
engine of the Texture Lab (chapter 75). The FPS walls in chapter 17 were a first
taste — a per-texel hash. Here we do it *properly*.

> **What "noise" must be.** Three properties: **deterministic** (same input → same
> output, always — no `rand()`), **smooth** (nearby inputs → nearby outputs, no
> speckle), and **controllable** (a `seed` and a `frequency` you can turn). Plain
> `hash(x,y)` has the first, not the second. That is the whole story below.

---

## 1. Random values are not noise

Start with a hash — a cheap function that scrambles two integers into a pseudo-random
32-bit number:

```cpp
std::uint32_t hash2(int x, int y, std::uint32_t seed) {
    std::uint32_t h = std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u
                    ^ (seed * 83492791u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
```

Divide by 2³² and you get a value in `[0,1)`. Deterministic ✓, controllable ✓ (the
`seed`). But if you evaluate it at *every pixel*, adjacent pixels are unrelated —
you get TV static, not a texture. **The missing property is smoothness.**

```
hash per pixel:        smooth noise:
█░▓█░░▓░█▓░█           ░░▒▒▓▓▓▒▒░░
▓█░▓█▓░█░▓█░    vs.    ░▒▒▓▓███▓▓▒
░▓█░░█▓█░█▓█           ▒▓▓████▓▓▒░
  (static)              (structure)
```

---

## 2. Value noise — random values on a lattice, interpolated

The idea: don't hash every pixel. Hash only the **integer lattice points**, then
**interpolate** smoothly between them. A pixel at `(x, y)` sits inside one lattice
cell; its value is a blend of the four corner hashes.

```
  (x0,y0)──────(x1,y0)      fx = x - floor(x)   (0..1 across the cell)
     │  •(x,y)    │          fy = y - floor(y)
     │            │          value = bilerp(corners, weights(fx,fy))
  (x0,y1)──────(x1,y1)
```

Two decisions make it smooth:

**Interpolation weight (the "fade").** Linear interpolation (`t`) gives creases at
the lattice lines — the slope jumps. Perlin's **quintic fade** `6t⁵−15t⁴+10t³` has
zero first *and* second derivative at `t=0` and `t=1`, so cells join seamlessly (C²
continuity):

```cpp
double fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
double lerp(double a, double b, double t) { return a + (b - a) * t; }
```

**Bilinear blend of the four corners:**

```cpp
double value_noise(double u, double v, int period, std::uint32_t seed) {
    const double x = u * period, y = v * period;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    const int x0 = wrap(ix, period), x1 = wrap(ix + 1, period);   // wrap: chapter 74
    const int y0 = wrap(iy, period), y1 = wrap(iy + 1, period);
    const double v00 = hash01(x0, y0, seed), v10 = hash01(x1, y0, seed);
    const double v01 = hash01(x0, y1, seed), v11 = hash01(x1, y1, seed);
    const double su = fade(fx), sv = fade(fy);
    return lerp(lerp(v00, v10, su), lerp(v01, v11, su), sv);      // in [0,1)
}
```

`period` is how many lattice cells span the `[0,1)` range — the **frequency**. Small
period = big blobs; large period = fine detail. (Ignore `wrap()` for now; chapter 74
explains why it makes the result tileable.)

**Worked example (1D, one cell).** Corners hash to `v0 = 0.2`, `v1 = 0.8`. At the
cell centre `fx = 0.5`: `fade(0.5) = 6(0.03125) − 15(0.0625) + 10(0.125) = 0.5`, so
`value = 0.2 + (0.8−0.2)·0.5 = 0.5`. At `fx = 0.1`: `fade(0.1) ≈ 0.00856`, so
`value ≈ 0.205` — barely moved off the corner, exactly the flat-then-steep S-curve
we want.

---

## 3. Perlin noise — gradients, not values

Value noise works, but the eye catches its lattice: blobs align to the grid, and the
value at each lattice point is fixed, so features cluster at integer coordinates.
**Perlin (gradient) noise** fixes this. Instead of a random *value* at each lattice
point, store a random *direction* (a gradient). The noise at `(x,y)` is the
**dot product** of each corner's gradient with the vector *from that corner to
(x,y)*, then interpolated:

```cpp
void grad(int x, int y, std::uint32_t seed, double& gx, double& gy) {
    static const double d[8][2] = { { 1, 0}, {-1, 0}, {0, 1}, {0,-1},
        {0.707,0.707},{-0.707,0.707},{0.707,-0.707},{-0.707,-0.707} };
    const int i = int(hash2(x, y, seed) & 7u);   // pick 1 of 8 compass directions
    gx = d[i][0]; gy = d[i][1];
}
```

```cpp
auto dot = [&](int gxi, int gyi, double dx, double dy) {
    double gx, gy; grad(gxi, gyi, seed, gx, gy); return gx * dx + gy * dy;
};
const double n00 = dot(x0, y0, fx,       fy);
const double n10 = dot(x1, y0, fx - 1.0, fy);
const double n01 = dot(x0, y1, fx,       fy - 1.0);
const double n11 = dot(x1, y1, fx - 1.0, fy - 1.0);
const double n = lerp(lerp(n00, n10, su), lerp(n01, n11, su), sv);  // ~[-1,1]
return n * 0.5 + 0.5;                                               // remap to [0,1]
```

Why it looks better: the dot product is **exactly 0 at every lattice point** (the
distance vector is zero there), so there are no fixed bright/dark spots on the grid —
the structure floats free of the lattice. The raw output is roughly `[-1, 1]`; we
remap to `[0, 1]` so it plugs into the same colour ramp as value noise.

```
value noise           Perlin noise
(grid-aligned blobs)  (organic, lattice-free)
 ▒▓▒ ▒▓▒ ▒▓▒           ░▒▓▓▒░  ▒▓░
 ▓█▓ ▓█▓ ▓█▓            ░▒▓████▓▒
 ▒▓▒ ▒▓▒ ▒▓▒           ▒▓▓▒░ ░▒▓▓
```

---

## 4. fBm — stacking octaves for detail

One layer of noise is smooth but featureless — clouds with no wisps, terrain with no
foothills. Real textures have detail *at many scales*. **Fractal Brownian motion
(fBm)** sums several noise layers ("octaves"), each at higher frequency and lower
amplitude:

```cpp
double fbm(double u, double v, Basis basis, int base_freq, int octaves,
           double gain, double lacunarity, std::uint32_t seed) {
    double sum = 0.0, amp = 1.0, norm = 0.0;
    int freq = base_freq;
    for (int o = 0; o < octaves; ++o) {
        const double n = (basis == Basis::Perlin) ? perlin_noise(u, v, freq, seed + o*101u)
                                                  : value_noise(u, v, freq, seed + o*101u);
        sum  += amp * n;          // add this octave, weighted by amplitude
        norm += amp;              // track total amplitude for normalization
        amp  *= gain;             // each octave quieter (gain < 1)
        freq  = int(std::lround(freq * lacunarity));   // each octave finer
    }
    return norm > 0.0 ? sum / norm : 0.0;              // normalized to [0,1]
}
```

- **lacunarity** (usually 2.0): how much finer each octave is. 2.0 = double the
  frequency each time.
- **gain** (usually 0.5): how much quieter each octave is. 0.5 = half the amplitude.
- **normalization** (`sum / norm`): without it the result exceeds `[0,1]` and the
  colour ramp clips. Dividing by the total amplitude keeps it in range.

**Worked amplitudes** for 4 octaves, gain 0.5: `1 + 0.5 + 0.25 + 0.125 = 1.875`. The
first octave carries the big shapes (53%), the rest add progressively finer grain.

```
octave 0 (freq 4)   +  octave 1 (freq 8)  + octave 2 (16) + ...  =  fBm
   ░▒▓▒░                 ▒▓░▒▓                ▒░▒▓░▒            ░▒▓█▓▒░
 amp 1.0                amp 0.5              amp 0.25          (rich detail)
```

---

## Pitfalls

- **Forgetting to normalize fBm** → values > 1 → the colour ramp saturates to a flat
  block. Always divide by the summed amplitude.
- **Linear interpolation instead of the quintic fade** → visible creases along
  lattice lines. Use `fade()`.
- **Signed vs unsigned noise.** Perlin is `[-1,1]`; value noise is `[0,1)`. Mixing
  them without remapping shifts the whole texture dark or bright. We remap Perlin
  with `*0.5 + 0.5`.
- **Integer overflow in the hash.** Multiply in `uint32_t` and let it wrap — that is
  intended; don't promote to signed.

## Glossary

- **Lattice** — the integer grid whose corners carry hashed values/gradients.
- **Fade / ease curve** — the S-shaped weight (`6t⁵−15t⁴+10t³`) that removes seams.
- **Octave** — one noise layer in an fBm sum.
- **Lacunarity / gain** — per-octave frequency multiplier / amplitude multiplier.
- **fBm** — fractal Brownian motion; a weighted sum of octaves.

## Exercises

1. **Turbulence.** Replace `n` in `fbm` with `std::fabs(2*n − 1)` (absolute value of
   signed noise). What changes? *(Hint: sharp ridges appear where noise crosses its
   midline — this is how you get marble and flame.)*
2. **Ridged noise.** Try `1 − std::fabs(2*n − 1)`. *(Hint: inverts the ridges into
   crests — mountain silhouettes.)*
3. **Domain warp.** Sample `fbm(u + fbm(u,v,...)*0.1, v, ...)`. *(Hint: feeding noise
   into the coordinates of more noise gives swirling, water-like distortion.)*
4. **Count the hashes.** For a 128×128 texture, 4-octave fBm, how many `hash2` calls?
   *(Hint: 4 corners × 4 octaves × 128² pixels ≈ 262k — still sub-millisecond.)*

Next: chapter 74 — how the `wrap()` we skipped makes every one of these tile
seamlessly.
