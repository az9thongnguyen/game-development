# 84 · 2D Lighting — additive radial lights

> Code: `src/engine/fx/light.{hpp,cpp}` · lib `light_core` · test `tests/test_light.cpp`
> New primitive: `Renderer2D::add_pixel` (`renderer2d.{hpp,cpp}`)
> Seen live: `./build/demo --light`

## Why this chapter exists

A flat-lit 2D scene reads as a diagram. The moment you add a *light* — a torch, a
muzzle flash, a neon sign, the glow of a fire — the same scene reads as a *place*.
This chapter gives the software renderer that capability: soft radial lights that
brighten a dark room where they reach. It is the next rung of Track A ("the engine
không chỉ 2d/3d") — a rendering feature that sits alongside the particle system,
not a new game.

## The model: additive light pooling

Our framebuffer is a CPU ARGB8888 buffer written one primitive at a time — there is
no post-process pass, no depth or stencil buffer, no shader stage. So we pick the
lighting model that fits that grain honestly:

1. Clear to a **dark ambient** (a near-black room).
2. Draw the scene **dim** (here, a lattice of muted tiles).
3. **Add** each light's radial contribution on top.

Because step 3 is *additive*, overlapping lights sum and blow out toward white —
exactly how additive light behaves in the real world (two torches are brighter
than one). What we deliberately **do not** do is cast shadows: there is no
occlusion test, so light passes through the tiles. Shadow casting needs a
visibility pass or a ray march per light and is a separate, much larger slice —
noted as a deferral, not smuggled in here.

## Two pieces: pure math, scene-side deposit

This is the same split the particle system uses (ch.79): the *math* is a pure core
that unit-tests headless; the *framebuffer write* is scene-side glue.

### The pure core — `light_core`

```cpp
struct Light { float x, y, radius; gfx::Color color; float intensity; };

float      light_falloff(float dist, float radius);
gfx::Color light_sample(const Light& L, float px, float py);
```

`light_falloff` is the shape of a light — how brightness fades from centre to edge:

```cpp
float light_falloff(float dist, float radius) {
    if (radius <= 0.0f) return 0.0f;
    const float x = dist / radius;
    if (x >= 1.0f) return 0.0f;
    const float k = 1.0f - x * x;
    return k * k;                 // 1 at x=0 → 0 at x=1, smooth, monotone
}
```

`(1 - (d/r)²)²` is cheap (no `pow`, no trig), pinned to **1 at the centre** and
**0 at the radius**, and monotonically decreasing between — the three properties
`test_light` enforces. It reads as a soft-edged glow rather than a hard disc.

`light_sample` turns that curve into something the renderer can deposit. The trick
is **which channel carries the weight**: it folds `intensity · falloff` into the
returned colour's **alpha**, keeping the light's RGB intact.

```cpp
float f = L.intensity * light_falloff(d, L.radius);
if (f > 1.0f) f = 1.0f;                                   // intensity>1 saturates
const auto a = uint8_t(f * 255.0f + 0.5f);
return gfx::rgba(r_of(L.color), g_of(L.color), b_of(L.color), a);
```

So a bright warm light near its centre returns `(255,170,90, 255)`; the same light
at 90% of its radius returns `(255,170,90, ~5)`. Same colour, tiny weight. That
alpha *is* how much of the light's colour gets added at that pixel.

### The renderer primitive — `add_pixel`

`Renderer2D` already had `blend_pixel` (alpha *over* — the source covers the
destination). Lighting needs the opposite: the source *adds* to the destination. So
we add its twin, `add_pixel`, backed by a physical sink `add_cov` that mirrors
`blend_cov`:

```cpp
void Renderer2D::add_cov(int x, int y, Color c, uint8_t coverage) {
    ...
    const uint32_t w = uint32_t(a_of(c)) * coverage / 255u;   // weight from alpha
    Color& dst = fb_.pixels[y * fb_.pitch + x];
    uint32_t r = r_of(dst) + r_of(c) * w / 255u;              // ADD, not blend
    uint32_t g = g_of(dst) + g_of(c) * w / 255u;
    uint32_t b = b_of(dst) + b_of(c) * w / 255u;
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;   // saturate
    dst = rgba(r, g, b, a_of(dst));                          // keep dst opaque
}
```

The saturation at 255 is what makes overlapping lights blow out to white instead of
wrapping around to black — the single most important line for it to *look* like
light. `add_pixel` is general (glows, fire, sparks), not lighting-specific, which
is why it lives on the renderer rather than in `light_core`.

## The scene-side loop

Because the core is pure, *drawing* the lights lives in the scene:

```cpp
for (const fx::Light& L : lights) {
    // clip a bounding box to the light's reach...
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const gfx::Color c = fx::light_sample(L, float(x), float(y));
            if (gfx::a_of(c)) g.add_pixel(x, y, c);     // alpha 0 → skip
        }
}
```

This is `O(radius²)` per light. For a handful of lights at 960×600 that is a few
hundred thousand adds a frame — nothing. If a scene ever wanted *hundreds* of
lights, the upgrade is to precompute each radius as a radial sprite once and blit
it additively; the `// ponytail:` comment in the code names that ceiling. We don't
build it now because nothing needs it — YAGNI.

## Seeing it: `--light`

The demo is a dark tiled room with three lights: a warm one that sits still, a cool
one that **drifts** left-to-right, and a white one that **follows the mouse** (its
radius and intensity are on sliders). The drift reuses the tween from ch.83 —
a ping-pong `SineInOut` `Tween` drives the cool light's X:

```cpp
drift_ = anim::Tween{0.0f, 1.0f, 3.2f, anim::Ease::SineInOut, /*pingpong=*/true};
...
lights_[1].x = 120.0f + drift_.value() * float(w_ - 240);
```

That is the two most recent Track-A slices composing: the *animation* primitive
moving the *lighting* primitive, no glue between them beyond a float. Drag the
intensity slider past 1 and watch the mouse light's core saturate to white — the
`add_cov` clamp doing its job.

## What was deliberately left out

- **Shadow casting / occlusion.** Needs a visibility pass per light; a separate
  slice. Today light passes through the tiles.
- **Normal maps** (per-pixel light direction), **coloured ambient / day-night
  ramp**, and **light as a sandbox component** — all future work, none blocked by
  this core.

## Try it

```sh
ctest --test-dir build -R '^light$' --output-on-failure   # the headless proof
./build/demo --light                                      # move the mouse; drag the sliders
```
