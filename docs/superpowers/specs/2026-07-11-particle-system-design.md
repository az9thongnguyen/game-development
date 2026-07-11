# Particle System — Design Spec

**Date:** 2026-07-11 · **Track:** A (engine depth) — sub-project 1 · **Status:** approved (auto), implementing.

## 1. Goal

A reusable **CPU particle system** in the engine: deterministic simulation any scene can drive,
rendered as alpha-fading blobs by our own `Renderer2D`. Ships with an interactive `--fx` playground
(fountain + click-bursts + live sliders) as the acceptance demo. First Track-A engine-depth feature.

## 2. Constraints

- **Pure sim, no renderer in the core.** `particles_core` (`engine/fx/particles.*`) integrates state
  only — no `Renderer2D`, no SDL, no IO — so it unit-tests headless (like `sandbox_core`). Drawing is a
  scene-side function over `Renderer2D`.
- **Deterministic.** A seeded xorshift32 RNG lives in the system; same seed + same call sequence →
  identical particles. Enables a determinism test and reproducible effects.
- **Fixed-step friendly.** `update(dt)` is called from a scene's `update()` (fixed 1/60), so the sim is
  frame-rate independent and deterministic.
- **Bounded.** A hard `max` cap on live particles; emission past the cap is dropped (never unbounded
  memory). The cap is logged in the demo, not silent.

## 3. Data

```cpp
struct Particle { float x, y, vx, vy; float age = 0, life = 1; float size = 3; gfx::Color c0, c1; };

struct EmitterConfig {
    float rate = 120;                 // particles/sec for continuous emission
    float life = 1.2f, life_var = 0.4f;
    float speed = 90, speed_var = 40;
    float dir = -1.5708f, spread = 0.5f;  // radians: default straight up, ±0.5 cone
    float gravity = 140;              // +y (down) accel, px/s^2
    float size0 = 4, size1 = 0;       // size at birth → death (lerped)
    gfx::Color c0 = gfx::rgb(255, 210, 120), c1 = gfx::rgb(200, 40, 20);  // birth → death colour
};
```

## 4. `ParticleSystem` (core)

```cpp
class ParticleSystem {
public:
    explicit ParticleSystem(uint32_t seed = 1, std::size_t max = 4000);
    void set_config(const EmitterConfig& c);
    void emit_burst(int n, float x, float y);          // n at once (explosion)
    void update(float dt, float ex, float ey, bool emitting);  // integrate + emit `rate` at (ex,ey)
    const std::vector<Particle>& particles() const;
    std::size_t alive() const;
    void clear();
};
```

- **Emission:** a fractional accumulator (`emit_acc += rate*dt`) spawns whole particles; each gets a
  randomized life/speed/direction/size/colour from the config. `emit_burst` ignores rate.
- **Integrate:** `vy += gravity*dt; x += vx*dt; y += vy*dt; age += dt`. Dead (`age >= life`) removed by
  swap-pop (order-independent, O(1) each). Cap: emission stops when `size() >= max`.
- **Fade (render helper, pure):** `current_color(p)` lerps `c0→c1` by `age/life` and scales alpha by
  `1 - age/life`; `current_size(p)` lerps `size0..size1`. In the core header (pure, testable), used by
  the draw code.

## 5. Rendering (scene-side)

`fx::draw(Renderer2D&, const ParticleSystem&)` — for each particle, `fill_circle(x, y, current_size,
current_color)` with the faded alpha (additive-ish via alpha blend). No new renderer primitive needed
(`fill_circle` + alpha blend already exist).

## 6. `--fx` demo scene

`FxScene` (SDL-touching): a `ParticleSystem`; a fountain emitter at bottom-centre (toggle); left-click
spawns a burst at the cursor; UI sliders for gravity / rate / spread / speed; a live particle-count +
cap label. `update(dt)`: `sys_.update(dt, fx, fy, fountain_)`. `render`: `fx::draw` + UI.

## 7. Tests (`tests/test_particles.cpp`, headless)

1. **burst** — `emit_burst(50,...)` → `alive()==50`; positions at the emit point.
2. **lifetime** — after `> max life` seconds of `update`, a burst fully expires (`alive()==0`).
3. **determinism** — two systems, same seed + identical calls → identical particle arrays.
4. **gravity** — a single upward particle's `vy` increases and it eventually falls (y returns/descends).
5. **cap** — bursting past `max` never exceeds `max`.
6. **fade helpers** — `current_color` alpha at `age=0` is full, near `0` at `age≈life`; `current_size`
   interpolates `size0→size1`.

## 8. Files

- Create: `src/engine/fx/particles.hpp`, `particles.cpp`, `tests/test_particles.cpp`
- Create: `src/games/fx/fx_scene.hpp`, `fx_scene.cpp`
- Modify: `CMakeLists.txt` (`particles_core`, `test_particles`, `fx_scene.cpp`→demo), `src/main.cpp` (`--fx`)
- Docs: guidebook `docs/book/79-particle-system.md`, README

## 9. Non-goals (deferred)

Textured particles (blit Lab `.hrt` per particle), additive-only blend mode flag, particle collision
with sandbox/fps geometry, GPU/SIMD batching, sub-emitters/trails, per-particle rotation.
