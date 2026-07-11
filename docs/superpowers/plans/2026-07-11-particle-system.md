# Particle System — Implementation Plan

> TDD, one behaviour per commit. Steps `- [ ]`.

**Goal:** Reusable deterministic CPU particle sim (`particles_core`) + a `--fx` playground.
**Architecture:** Pure sim in `engine/fx/particles.*` (no Renderer2D → headless-testable); a scene-side
`fx::draw` over `Renderer2D`; a `FxScene` demo. Determinism via a seeded xorshift32. Deferrals: spec §9.

---

### Task 1: `particles_core` — the pure sim + fade helpers

**Files:** Create `src/engine/fx/particles.hpp`, `src/engine/fx/particles.cpp`, `tests/test_particles.cpp`;
Modify `CMakeLists.txt`.

- [ ] Write `test_particles.cpp` first (burst count, lifetime expiry, determinism, gravity, cap, fade
  helpers) per spec §7. CHECK-macro harness like test_sandbox.
- [ ] Implement `particles.hpp` (Particle, EmitterConfig, ParticleSystem API, inline `current_color`/
  `current_size` pure helpers) and `particles.cpp` (xorshift32 RNG, emit accumulator, integrate, swap-pop
  reap, cap).
- [ ] CMake: `particles_core` STATIC (`particles.cpp`, link engine_flags, include src); `test_particles`
  (link particles_core engine_flags; needs no renderer/assets); `add_test(NAME particles ...)`.
- [ ] `cmake -B build && ctest -R particles` → PASS. Commit.

Key impl notes (complete):

```cpp
// particles.hpp
#pragma once
#include <cstdint>
#include <vector>
#include "engine/color.hpp"
namespace fx {
struct Particle { float x=0,y=0,vx=0,vy=0; float age=0,life=1; float size0=4,size1=0;
                  gfx::Color c0=gfx::rgb(255,210,120), c1=gfx::rgb(200,40,20); };
struct EmitterConfig { float rate=120, life=1.2f, life_var=0.4f, speed=90, speed_var=40,
                       dir=-1.5708f, spread=0.5f, gravity=140, size0=4, size1=0;
                       gfx::Color c0=gfx::rgb(255,210,120), c1=gfx::rgb(200,40,20); };
float t_of(const Particle& p);                      // age/life clamped [0,1]
gfx::Color current_color(const Particle& p);        // c0->c1, alpha fades to 0
float      current_size(const Particle& p);         // size0->size1
class ParticleSystem {
public:
    explicit ParticleSystem(std::uint32_t seed=1, std::size_t max=4000);
    void set_config(const EmitterConfig& c) { cfg_ = c; }
    const EmitterConfig& config() const { return cfg_; }
    EmitterConfig& config() { return cfg_; }
    void emit_burst(int n, float x, float y);
    void update(float dt, float ex, float ey, bool emitting);
    const std::vector<Particle>& particles() const { return ps_; }
    std::size_t alive() const { return ps_.size(); }
    void clear() { ps_.clear(); }
private:
    void spawn_one(float x, float y);
    std::uint32_t rng();                            // xorshift32
    float frand();                                  // [0,1)
    float vary(float base, float var);              // base + U(-var,var)
    EmitterConfig cfg_;
    std::vector<Particle> ps_;
    std::size_t max_;
    std::uint32_t state_;
    float emit_acc_ = 0;
};
} // namespace fx
```

```cpp
// particles.cpp
#include "engine/fx/particles.hpp"
#include <algorithm>
#include <cmath>
namespace fx {
static uint8_t lerp8(uint8_t a, uint8_t b, float t){ return uint8_t(a + (b-a)*t + 0.5f); }
float t_of(const Particle& p){ float t = p.life>0 ? p.age/p.life : 1.f; return t<0?0:(t>1?1:t); }
gfx::Color current_color(const Particle& p){
    const float t = t_of(p);
    const uint8_t r=lerp8(gfx::r_of(p.c0),gfx::r_of(p.c1),t), g=lerp8(gfx::g_of(p.c0),gfx::g_of(p.c1),t),
                  b=lerp8(gfx::b_of(p.c0),gfx::b_of(p.c1),t);
    const uint8_t a = uint8_t((1.f - t) * 255.f + 0.5f);
    return gfx::rgba(r,g,b,a);
}
float current_size(const Particle& p){ const float t=t_of(p); return p.size0 + (p.size1-p.size0)*t; }

ParticleSystem::ParticleSystem(uint32_t seed, std::size_t max)
    : max_(max), state_(seed ? seed : 1) {}
uint32_t ParticleSystem::rng(){ uint32_t x=state_; x^=x<<13; x^=x>>17; x^=x<<5; return state_=x; }
float ParticleSystem::frand(){ return (rng() >> 8) * (1.0f/16777216.0f); }   // 24-bit [0,1)
float ParticleSystem::vary(float base, float var){ return base + (frand()*2.f-1.f)*var; }

void ParticleSystem::spawn_one(float x, float y){
    if (ps_.size() >= max_) return;
    Particle p; p.x=x; p.y=y;
    const float a = cfg_.dir + (frand()*2.f-1.f)*cfg_.spread;
    const float sp = std::max(0.f, vary(cfg_.speed, cfg_.speed_var));
    p.vx = std::cos(a)*sp; p.vy = std::sin(a)*sp;
    p.life = std::max(0.05f, vary(cfg_.life, cfg_.life_var));
    p.size0 = cfg_.size0; p.size1 = cfg_.size1; p.c0 = cfg_.c0; p.c1 = cfg_.c1;
    ps_.push_back(p);
}
void ParticleSystem::emit_burst(int n, float x, float y){ for (int i=0;i<n;++i) spawn_one(x,y); }

void ParticleSystem::update(float dt, float ex, float ey, bool emitting){
    if (emitting && cfg_.rate > 0){
        emit_acc_ += cfg_.rate * dt;
        while (emit_acc_ >= 1.f){ emit_acc_ -= 1.f; spawn_one(ex, ey); }
    }
    for (std::size_t i=0; i<ps_.size();){
        Particle& p = ps_[i];
        p.vy += cfg_.gravity * dt;
        p.x  += p.vx * dt; p.y += p.vy * dt; p.age += dt;
        if (p.age >= p.life){ ps_[i] = ps_.back(); ps_.pop_back(); }  // swap-pop
        else ++i;
    }
}
} // namespace fx
```

Test assertions (complete) in `tests/test_particles.cpp`:

```cpp
#include "engine/fx/particles.hpp"
#include <cstdio>
using namespace fx;
static int g_failures=0;
#define CHECK(c) do{ if(!(c)){ std::printf("FAIL %s:%d: %s\n",__FILE__,__LINE__,#c); ++g_failures; } }while(0)
int main(){
    // burst
    { ParticleSystem s; s.emit_burst(50, 10, 20);
      CHECK(s.alive()==50); CHECK(s.particles()[0].x==10 && s.particles()[0].y==20); }
    // lifetime expiry: no emission, step past max life
    { EmitterConfig c; c.gravity=0; ParticleSystem s; s.set_config(c); s.emit_burst(30, 0,0);
      for (int i=0;i<200;++i) s.update(0.02f, 0,0, false);   // 4s > life+var
      CHECK(s.alive()==0); }
    // determinism
    { ParticleSystem a(7), b(7);
      for (int i=0;i<60;++i){ a.update(0.016f, 5,5, true); b.update(0.016f, 5,5, true); }
      CHECK(a.alive()==b.alive() && a.alive()>0);
      bool same=true; for (size_t i=0;i<a.alive();++i){ if (a.particles()[i].x!=b.particles()[i].x || a.particles()[i].y!=b.particles()[i].y) same=false; }
      CHECK(same); }
    // gravity pulls down: one particle emitted straight up eventually descends
    { EmitterConfig c; c.rate=0; c.spread=0; c.speed=50; c.speed_var=0; c.life=5; c.life_var=0; c.dir=-1.5708f;
      ParticleSystem s; s.set_config(c); s.emit_burst(1, 0, 0);
      float ymin=0; for (int i=0;i<120;++i){ s.update(0.016f,0,0,false); if (s.alive()) ymin=std::min(ymin,s.particles()[0].y); }
      CHECK(s.alive()==1); CHECK(s.particles()[0].vy>0); CHECK(s.particles()[0].y > ymin); }  // rose then fell
    // cap
    { ParticleSystem s(1, 100); s.emit_burst(500, 0,0); CHECK(s.alive()==100); }
    // fade helpers
    { Particle p; p.life=1; p.age=0; CHECK(gfx::a_of(current_color(p))==255);
      p.age=1; CHECK(gfx::a_of(current_color(p))==0);
      p.size0=8; p.size1=0; p.age=0; CHECK(current_size(p)==8); p.age=1; CHECK(current_size(p)==0); }
    if (g_failures==0) std::printf("particles: all tests passed\n");
    else               std::printf("particles: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
```

(Add `#include <algorithm>` for std::min in the test.)

### Task 2: `--fx` demo scene + draw

**Files:** Create `src/games/fx/fx_scene.{hpp,cpp}`; Modify `CMakeLists.txt` (demo src + link particles_core),
`src/main.cpp` (`--fx`, 960×600 smooth/highdpi/ss=kAA).

- [ ] `fx::draw(Renderer2D& g, const ParticleSystem& s)`: `for (p) g.fill_circle(int(p.x),int(p.y),
  std::max(1,int(current_size(p))), current_color(p));` — put in fx_scene.cpp (demo side).
- [ ] `FxScene`: members `ParticleSystem sys_`, `ui::Context ui_`, `bool fountain_=true`, mirror-floats for
  sliders, `int w_,h_`. `update(dt)`: `sys_.update(float(dt), fountain_x, h_-30, fountain_)`. `render`:
  clear dark, draw particles, UI panel (Fountain toggle, gravity/rate/spread/speed sliders that write
  `sys_.config()`), count/cap label; left-click (not over UI) → `sys_.emit_burst(80, mx, my)`.
- [ ] Build demo clean; `ctest` all green. Commit.

### Task 3: docs + merge

- [ ] Guidebook `docs/book/79-particle-system.md` (pure-sim/scene-draw split, deterministic RNG, emit
  accumulator, swap-pop reap, fade, the cap; pitfalls; exercises).
- [ ] README roadmap row + `--fx` run line.
- [ ] ASan/UBSan on `test_particles`. Merge `--no-ff`; delete branch; checkpoint memory.

## Self-review

- Spec §3/§4 → T1 types/sim; §5/§6 → T2 draw/scene; §7 → T1 tests.
- Type consistency: `ParticleSystem(seed,max)`, `update(dt,ex,ey,emitting)`, `emit_burst(n,x,y)`,
  `current_color/current_size(Particle)` identical across header/cpp/test/scene.
- Needs `gfx::rgba`/`a_of`/`r_of`/`g_of`/`b_of` (exist in color.hpp — verify before impl).
