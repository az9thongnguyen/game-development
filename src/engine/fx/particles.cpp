// =============================================================================
//  engine/fx/particles.cpp
// =============================================================================
#include "engine/fx/particles.hpp"

#include <algorithm>
#include <cmath>

namespace fx {

namespace {
uint8_t lerp8(uint8_t a, uint8_t b, float t) {
    return static_cast<uint8_t>(a + (int(b) - int(a)) * t + 0.5f);
}
} // namespace

float t_of(const Particle& p) {
    const float t = p.life > 0 ? p.age / p.life : 1.0f;
    return t < 0 ? 0 : (t > 1 ? 1 : t);
}

gfx::Color current_color(const Particle& p) {
    const float t = t_of(p);
    const uint8_t r = lerp8(gfx::r_of(p.c0), gfx::r_of(p.c1), t);
    const uint8_t g = lerp8(gfx::g_of(p.c0), gfx::g_of(p.c1), t);
    const uint8_t b = lerp8(gfx::b_of(p.c0), gfx::b_of(p.c1), t);
    const uint8_t a = static_cast<uint8_t>((1.0f - t) * 255.0f + 0.5f);
    return gfx::rgba(r, g, b, a);
}

float current_size(const Particle& p) {
    const float t = t_of(p);
    return p.size0 + (p.size1 - p.size0) * t;
}

ParticleSystem::ParticleSystem(std::uint32_t seed, std::size_t max)
    : max_(max), state_(seed ? seed : 1) {}

std::uint32_t ParticleSystem::rng() {
    std::uint32_t x = state_;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return state_ = x;
}
float ParticleSystem::frand() { return (rng() >> 8) * (1.0f / 16777216.0f); }  // 24-bit [0,1)
float ParticleSystem::vary(float base, float var) { return base + (frand() * 2.0f - 1.0f) * var; }

void ParticleSystem::spawn_one(float x, float y) {
    if (ps_.size() >= max_) return;
    Particle p;
    p.x = x; p.y = y;
    const float a  = cfg_.dir + (frand() * 2.0f - 1.0f) * cfg_.spread;
    const float sp = std::max(0.0f, vary(cfg_.speed, cfg_.speed_var));
    p.vx = std::cos(a) * sp;
    p.vy = std::sin(a) * sp;
    p.life  = std::max(0.05f, vary(cfg_.life, cfg_.life_var));
    p.size0 = cfg_.size0; p.size1 = cfg_.size1;
    p.c0 = cfg_.c0; p.c1 = cfg_.c1;
    ps_.push_back(p);
}

void ParticleSystem::emit_burst(int n, float x, float y) {
    for (int i = 0; i < n; ++i) spawn_one(x, y);
}

void ParticleSystem::update(float dt, float ex, float ey, bool emitting) {
    if (emitting && cfg_.rate > 0) {
        emit_acc_ += cfg_.rate * dt;
        while (emit_acc_ >= 1.0f) { emit_acc_ -= 1.0f; spawn_one(ex, ey); }
    }
    for (std::size_t i = 0; i < ps_.size();) {
        Particle& p = ps_[i];
        p.vy += cfg_.gravity * dt;
        p.x  += p.vx * dt;
        p.y  += p.vy * dt;
        p.age += dt;
        if (p.age >= p.life) { ps_[i] = ps_.back(); ps_.pop_back(); }  // swap-pop: O(1), order-free
        else ++i;
    }
}

} // namespace fx
