// =============================================================================
//  engine/fx/particles.hpp  —  a small, deterministic CPU particle system
// =============================================================================
//  Pure simulation: no Renderer2D, no SDL, no IO — so it unit-tests headless and
//  any scene can own one and drive it from update(dt). Emission and motion are
//  driven by a seeded xorshift32 RNG, so the same seed + same calls reproduce the
//  same particles exactly. Drawing is a scene-side function over Renderer2D.
//  See docs/book/79-particle-system.md.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/color.hpp"

namespace fx {

struct Particle {
    float x = 0, y = 0, vx = 0, vy = 0;
    float age = 0, life = 1;                 // seconds lived / total lifetime
    float size0 = 4, size1 = 0;              // radius at birth → death (px)
    gfx::Color c0 = gfx::rgb(255, 210, 120); // colour at birth
    gfx::Color c1 = gfx::rgb(200, 40, 20);   // colour at death
};

struct EmitterConfig {
    float rate     = 120;                    // particles/sec for continuous emission
    float life     = 1.2f, life_var  = 0.4f;
    float speed    = 90,   speed_var = 40;
    float dir      = -1.5708f, spread = 0.5f; // radians: default straight up (-pi/2), ±spread cone
    float gravity  = 140;                    // +y (down) acceleration, px/s^2
    float size0    = 4, size1 = 0;           // birth → death radius
    gfx::Color c0 = gfx::rgb(255, 210, 120), c1 = gfx::rgb(200, 40, 20);
};

// Pure render helpers (no state): normalized life, faded colour, lerped size.
float      t_of(const Particle& p);          // age/life clamped to [0,1]
gfx::Color current_color(const Particle& p); // c0->c1 by t, alpha fades 255->0
float      current_size(const Particle& p);  // size0->size1 by t

class ParticleSystem {
public:
    explicit ParticleSystem(std::uint32_t seed = 1, std::size_t max = 4000);

    void set_config(const EmitterConfig& c) { cfg_ = c; }
    const EmitterConfig& config() const { return cfg_; }
    EmitterConfig&       config()       { return cfg_; }   // scenes tweak via sliders

    void emit_burst(int n, float x, float y);              // n at once (explosion)
    void update(float dt, float ex, float ey, bool emitting);  // integrate + emit rate at (ex,ey)

    const std::vector<Particle>& particles() const { return ps_; }
    std::size_t alive() const { return ps_.size(); }
    std::size_t capacity() const { return max_; }
    void clear() { ps_.clear(); }

private:
    void          spawn_one(float x, float y);
    std::uint32_t rng();                       // xorshift32
    float         frand();                     // [0,1)
    float         vary(float base, float var); // base + U(-var,+var)

    EmitterConfig         cfg_;
    std::vector<Particle> ps_;
    std::size_t           max_;
    std::uint32_t         state_;
    float                 emit_acc_ = 0;
};

} // namespace fx
