# 2D Lighting — design

**Track A (engine depth). Date: 2026-07-11.**

## Goal

Give the software renderer **additive radial lights**: soft glows that brighten a
dark scene where they reach. This is the next rung of "engine không chỉ 2d/3d" —
a rendering capability (not just flat sprites) that any 2D game wants for
atmosphere (torches, explosions, neon, day/night).

## Approach (why additive, not shadow-casting)

The framebuffer is CPU ARGB8888 with per-primitive writes (no post-process pass,
no depth/stencil). The lean, faithful model for that is **additive light pooling**:
clear to a dark ambient, draw the scene dim, then *add* each light's radial
contribution on top. Stacked lights sum and blow out to white — exactly how real
additive light reads. **No shadow casting / occlusion** (that needs a visibility
pass or ray march — a separate, bigger slice; noted as a deferral).

## Design

Two pieces, mirroring the particle split (pure core + scene-side draw):

### Pure core — `engine/fx/light.{hpp,cpp}` (`light_core`)

```
struct Light { float x,y,radius; gfx::Color color; float intensity; };
float      light_falloff(float dist, float radius);   // 1 at 0, 0 at/past radius, monotone
gfx::Color light_sample(const Light&, float px, float py);  // color w/ alpha = intensity*falloff
```

- Falloff is `(1 - (d/r)^2)^2` — cheap, soft-edged, monotonically decreasing,
  pinned to 1 at centre and 0 at the radius. `radius<=0` guarded to 0.
- `light_sample` folds intensity·falloff into the returned colour's **alpha**,
  which the renderer reads as the additive weight. Intensity>1 saturates at 255.
- Pure: no SDL, no RNG — unit-tests headless.

### Renderer primitive — `Renderer2D::add_pixel` (+ private `add_cov`)

The additive counterpart of `blend_pixel`/`blend_cov`: `dst.rgb += c.rgb *
(c.alpha·coverage)`, saturating at 255, leaving dst alpha alone. ss-aware like the
rest of the API. General-purpose (glows, fire, sparks), not lighting-specific.

### Scene glue — `--light` (`games/light/light_scene.{hpp,cpp}`)

A dark tiled room lit by three lights: a warm static one, a cool one that drifts
via a **ping-pong `SineInOut` tween** (reuses `tween_core`), and a white light
that follows the mouse (radius/intensity on sliders). `draw_lights` loops each
light's bounding box, samples the pure core, and deposits via `add_pixel`.
`supersample=1` (soft lights don't need SSAA; keeps the per-pixel add cheap).

## Testing

`tests/test_light.cpp`, dependency-free:
- `light_falloff`: 1 at centre, 0 at/past radius, radius<=0 guard, monotone sweep,
  strict interior value.
- `light_sample`: max alpha + RGB preserved at centre; alpha 0 past radius;
  intensity scales weight; intensity>1 saturates at 255.

## Deferrals (ponytail)

Shadow casting / occluders; normal-mapped lights; coloured ambient/day-night ramp;
precomputed radial-sprite blit if the light count ever gets large (the per-pixel
loop is O(radius²)·lights); light as a sandbox component.
