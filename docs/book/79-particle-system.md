# Chapter 79 — A CPU Particle System

> Code: `src/engine/fx/particles.{hpp,cpp}` (`particles_core`),
> `src/games/fx/fx_scene.{hpp,cpp}`; tests `tests/test_particles.cpp`; run
> `./build/demo --fx`.

Every effect that *feels* alive — sparks, smoke, a fountain, an explosion — is the same
trick: a few hundred tiny things, each following a dumb rule, redrawn every frame. This
chapter builds that as a reusable engine module. It is the first **Track A** feature —
depth in the engine itself, not another game — and the whole design turns on one split we
have now made three times: **the simulation is pure; the drawing is not.**

## 1. The split, one more time

A particle system is tempting to write as one class that owns pixels. Don't. Two jobs live
here, and they belong on opposite sides of the SDL seam:

- **Simulate** — spawn particles, move them, age them, recycle the dead. Pure arithmetic on
  a `std::vector<Particle>`. No `Renderer2D`, no framebuffer, no files. This is
  `particles_core`, and it unit-tests with no window (exactly like `sandbox_core` and
  `maplab_core`).
- **Draw** — turn each live particle into a blob on screen. This *needs* `Renderer2D`, so it
  is a scene-side function, `draw()` in `fx_scene.cpp`.

Because the sim carries no renderer dependency, `test_particles` links `particles_core` and
nothing else, and asserts real behaviour — counts, lifetimes, determinism — headless.

## 2. A particle, and its emitter's recipe

```cpp
struct Particle { float x,y, vx,vy; float age=0, life=1; float size0=4,size1=0;
                  gfx::Color c0, c1; };
```

A particle is deliberately fat with *its own* end-state (`life`, `size0/1`, `c0/1`) rather
than pointing back at its emitter. Once born it is autonomous — the emitter can change its
config, or be destroyed, and particles already in flight finish their lives unchanged. That
independence is what lets `emit_burst` and a running fountain coexist with different looks.

The `EmitterConfig` is the *recipe* for new particles: emission `rate`, a base value and a
`_var` spread for life/speed, a `dir`+`spread` cone, `gravity`, and the birth→death size and
colour. Every new particle samples the recipe once and then owns the result.

## 3. Deterministic randomness

Particles need variety — but *reproducible* variety, so effects look the same every run and
so the sim is testable. The system owns a seeded **xorshift32**:

```cpp
uint32_t ParticleSystem::rng() { uint32_t x=state_; x^=x<<13; x^=x>>17; x^=x<<5; return state_=x; }
float    frand() { return (rng() >> 8) * (1.0f/16777216.0f); }   // top 24 bits -> [0,1)
```

Three lines, no `<random>`, no global state. Same seed + same sequence of `update`/`emit`
calls ⇒ byte-identical particles — which is exactly what `test_particles`' determinism case
asserts (two systems seeded alike march in lockstep). Using the *top* bits (`>> 8`) matters:
xorshift's low bits are the weakest, so we discard them.

## 4. Emission: a fractional accumulator

`rate` is particles-per-second, but a frame only advances `dt` seconds — usually a fraction
of a particle's worth. Rounding each frame would make emission frame-rate dependent and
lumpy. Instead, carry the remainder:

```cpp
emit_acc_ += rate * dt;
while (emit_acc_ >= 1.0f) { emit_acc_ -= 1.0f; spawn_one(ex, ey); }
```

At 300/s and dt=1/60 that adds 5.0 per frame → 5 particles, remainder 0. At 70/s it adds
1.16… per frame → mostly 1, occasionally 2, and the fractional debt never gets lost. Smooth
emission at any rate and any frame time. `emit_burst(n)` bypasses the accumulator — an
explosion is *n* now, not a rate.

## 5. Integrate, then reap with swap-pop

```cpp
for (std::size_t i = 0; i < ps_.size();) {
    Particle& p = ps_[i];
    p.vy += gravity*dt;  p.x += p.vx*dt;  p.y += p.vy*dt;  p.age += dt;
    if (p.age >= p.life) { ps_[i] = ps_.back(); ps_.pop_back(); }  // swap-pop
    else ++i;
}
```

Semi-implicit Euler (update velocity, then position) — cheap and stable enough for effects.
The removal is the part worth naming: **swap-pop**. To delete element `i`, overwrite it with
the last element and shrink the vector — O(1), no shifting the tail. We *don't* advance `i`
when we do this, because a fresh (unprocessed) particle now sits at `i`. Particle order
doesn't matter for additive-ish blobs, so trading order for speed is free here. (Contrast the
sandbox's reap, ch.76, which deferred edits through a command buffer because it was iterating
*views*; here we own the vector outright, so in-place swap-pop is simplest.)

## 6. Bounded by construction

A stuck emitter or a fat-fingered `rate` must never eat memory. `spawn_one` refuses past a
hard `max_`:

```cpp
void spawn_one(...) { if (ps_.size() >= max_) return; ... }
```

The cap is real and *visible* — the `--fx` HUD prints `particles: N / cap`, so a dropped
spawn is observable, not silent. `test_particles` bursts 500 into a cap-100 system and asserts
exactly 100 survive.

## 7. Fade, purely

Colour and size are functions of a particle's *normalized age* `t = age/life ∈ [0,1]`, and
they are pure helpers in the core header so both the drawer and the tests can call them:

```cpp
float      t_of(const Particle&);          // clamped age/life
gfx::Color current_color(const Particle&); // c0->c1 by t; alpha = (1-t)*255
float      current_size(const Particle&);  // size0->size1 by t
```

The alpha ramp is what sells the effect: a particle is opaque at birth and fades to nothing
at death, so it dissolves instead of blinking out. The scene draws it in one line:

```cpp
for (const Particle& p : s.particles())
    g.fill_circle(int(p.x), int(p.y), std::max(1, int(current_size(p))), current_color(p));
```

`fill_circle` already alpha-blends (ch.68), so overlapping faded blobs pile up into the soft,
glowy look — no new renderer primitive needed.

## 8. Where it runs

`FxScene` runs the sim in `update()` (fixed 1/60, deterministic) and draws in `render()` —
the same discipline as the sandbox. The fountain emits from the bottom-centre each step;
left-click fires an 80-particle burst at the cursor; sliders write straight into
`sys_.config()` so tuning is live. That is the whole playground: a thin skin over a pure core.

## Pitfalls

- **Renderer in the core.** Put `fill_circle` in `particles_core` and the headless test won't
  link and the web story rots. Sim simulates; the scene draws.
- **Rounding emission per frame.** Frame-rate-dependent, lumpy output. Keep the fractional
  accumulator.
- **Advancing the index after a swap-pop.** You'd skip the particle you just moved into slot
  `i`. Only `++i` when you *keep* the particle.
- **Unbounded pools.** Always cap, and surface the cap so a drop is visible.
- **Low-bit RNG.** xorshift's low bits are weak; sample the high bits.

## Glossary

- **xorshift32** — a 3-shift integer PRNG; fast, seeded, deterministic.
- **emit accumulator** — the carried fractional-particle debt that makes `rate` frame-rate
  independent.
- **swap-pop** — O(1) vector removal: overwrite with the last element, shrink.
- **normalized age (`t`)** — `age/life`; drives every fade.

## Exercises

1. **Textured particles.** Blit a Texture Lab `.hrt` (scaled by `current_size`, tinted by
   `current_color`) instead of a flat circle — the join of fx with the Texture Lab. Which
   side gets the image lookup?
2. **Additive vs alpha.** Add a blend-mode flag so sparks glow additively while smoke stays
   alpha. What changes in `draw`, and does the core care?
3. **Wind.** Add a per-frame force vector (not just gravity). Where does it live — config or a
   `update` argument — and why?
4. **Sub-emitters.** Make some particles spawn a small burst when they die (a firework).
   Where in `update` does that hook in without breaking the swap-pop loop?
