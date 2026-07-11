# Mini Studio — Texture Lab Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `./build/demo --studio` — an interactive tool that generates seamless, tileable textures procedurally (hand-written noise) and saves them into the asset library as `.hrt` with re-editable recipes.

**Architecture:** A pure, SDL-free `studio_core` static lib (noise → texture_gen → recipe), unit-tested headless like the other `*_core` libs; a new `encode_hrt` closes the write half of the existing image seam; a `studio::StudioScene` drawn by the existing `Renderer2D` + immediate-mode `ui::Context` hosts a live preview and an in-session collection browser.

**Tech Stack:** C++20, CMake, CTest (assert-based, no framework), `gfx::Image`/`gfx::Color`, `ui::Context`, `assets::` I/O seam. SDL only via the platform layer (studio_core touches none).

**Conventions locked from the codebase:**
- `gfx::Color = uint32_t` (`0xAARRGGBB`); helpers `gfx::rgb/rgba/r_of/g_of/b_of/a_of` in `engine/color.hpp`.
- `gfx::Image{ int w,h; std::vector<Color> pixels; }` (ARGB8888, row-major) in `engine/image.hpp`.
- `.hrt` bytes = `"HRT1"` + BE u32 w + BE u32 h + `R,G,B,A` per pixel (`decode_hrt` in `engine/image.cpp`).
- Tests use the `CHECK(cond)` macro + `approx()` and `return g_failures;` (see `tests/test_fps.cpp`).
- Scene/UI wiring pattern = `src/games/editor/editor_scene.cpp`.
- New content-tool code lives under `src/games/studio/` (mirrors `viz3d_core` living under `src/games/viz3d/`).

**Branch:** `feat/studio-texture-lab` (already created; the design spec is committed there).

---

## File Structure

| File | Responsibility |
|------|----------------|
| `src/games/studio/noise.hpp/.cpp` | Value + Perlin + fBm noise, torus-periodic (seamless). Pure. |
| `src/games/studio/texture_gen.hpp/.cpp` | `TextureParams` → `gfx::Image` (base pattern + ramp + operator). Pure. |
| `src/games/studio/recipe.hpp/.cpp` | `TextureParams` ⇄ `key=value` text sidecar (re-editable). Pure. |
| `src/engine/image.hpp/.cpp` | ADD `encode_hrt(const Image&)` (mirror of `decode_hrt`). |
| `src/games/studio/studio_scene.hpp/.cpp` | The `--studio` scene: params panel, live preview, save, collection browser. |
| `tests/test_studio.cpp` | Headless tests for noise/texture/recipe/encode round-trip. |
| `CMakeLists.txt` | `studio_core` lib + `test_studio` + add scene to `demo`. |
| `src/main.cpp` | `--studio` dispatch. |

---

## Task 1: Noise core (value, Perlin, fBm; torus-periodic)

**Files:**
- Create: `src/games/studio/noise.hpp`, `src/games/studio/noise.cpp`
- Create: `tests/test_studio.cpp`
- Modify: `CMakeLists.txt` (add `studio_core` + `test_studio`)

- [ ] **Step 1: Create the header**

Create `src/games/studio/noise.hpp`:

```cpp
// =============================================================================
//  games/studio/noise.hpp  —  seamless procedural noise (hand-written)
// =============================================================================
//  All bases are PERIODIC with period 1.0 in u and v: noise(u,v)==noise(u+1,v)
//  ==noise(u,v+1). That periodicity is exactly what makes a texture sampled over
//  [0,1)^2 tile seamlessly — the seam between adjacent tiles is continuous.
// =============================================================================
#pragma once
#include <cstdint>

namespace studio {

// Value noise at (u,v), lattice wrapped on a `period`x`period` torus. Result in [0,1).
double value_noise(double u, double v, int period, std::uint32_t seed);

// Gradient (Perlin) noise, same torus wrap. Result remapped from [-1,1] to [0,1].
double perlin_noise(double u, double v, int period, std::uint32_t seed);

enum class Basis { Value, Perlin };

// Fractal Brownian motion over [0,1)^2: `octaves` layers, frequency *= lacunarity
// and amplitude *= gain each octave, normalized to [0,1]. base_freq (>=1) is the
// lattice size of octave 0; INTEGER lacunarity keeps every octave tileable.
double fbm(double u, double v, Basis basis, int base_freq, int octaves,
           double gain, double lacunarity, std::uint32_t seed);

} // namespace studio
```

- [ ] **Step 2: Create the implementation**

Create `src/games/studio/noise.cpp`:

```cpp
// =============================================================================
//  games/studio/noise.cpp
// =============================================================================
#include "games/studio/noise.hpp"

#include <cmath>

namespace studio {
namespace {

// 2D integer hash -> uint32 (same family as fps::hash2, plus a seed term).
std::uint32_t hash2(int x, int y, std::uint32_t seed) {
    std::uint32_t h = std::uint32_t(x) * 73856093u ^ std::uint32_t(y) * 19349663u
                    ^ (seed * 83492791u);
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h;
}
int    wrap(int a, int p)                          { int m = a % p; return m < 0 ? m + p : m; }
double fade(double t)                              { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
double lerp(double a, double b, double t)          { return a + (b - a) * t; }
double hash01(int x, int y, std::uint32_t seed)    { return hash2(x, y, seed) / 4294967296.0; }

void grad(int x, int y, std::uint32_t seed, double& gx, double& gy) {
    static const double d[8][2] = {
        { 1, 0}, {-1, 0}, {0, 1}, {0,-1},
        { 0.70710678, 0.70710678}, {-0.70710678, 0.70710678},
        { 0.70710678,-0.70710678}, {-0.70710678,-0.70710678}};
    const int i = int(hash2(x, y, seed) & 7u);
    gx = d[i][0]; gy = d[i][1];
}

} // namespace

double value_noise(double u, double v, int period, std::uint32_t seed) {
    if (period < 1) period = 1;
    const double x = u * period, y = v * period;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    const int x0 = wrap(ix, period), x1 = wrap(ix + 1, period);
    const int y0 = wrap(iy, period), y1 = wrap(iy + 1, period);
    const double v00 = hash01(x0, y0, seed), v10 = hash01(x1, y0, seed);
    const double v01 = hash01(x0, y1, seed), v11 = hash01(x1, y1, seed);
    const double su = fade(fx), sv = fade(fy);
    return lerp(lerp(v00, v10, su), lerp(v01, v11, su), sv);
}

double perlin_noise(double u, double v, int period, std::uint32_t seed) {
    if (period < 1) period = 1;
    const double x = u * period, y = v * period;
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double fx = x - ix, fy = y - iy;
    const int x0 = wrap(ix, period), x1 = wrap(ix + 1, period);
    const int y0 = wrap(iy, period), y1 = wrap(iy + 1, period);
    auto dot = [&](int gxi, int gyi, double dx, double dy) {
        double gx, gy; grad(gxi, gyi, seed, gx, gy); return gx * dx + gy * dy;
    };
    const double n00 = dot(x0, y0, fx,       fy);
    const double n10 = dot(x1, y0, fx - 1.0, fy);
    const double n01 = dot(x0, y1, fx,       fy - 1.0);
    const double n11 = dot(x1, y1, fx - 1.0, fy - 1.0);
    const double su = fade(fx), sv = fade(fy);
    const double n = lerp(lerp(n00, n10, su), lerp(n01, n11, su), sv);  // ~[-1,1]
    return n * 0.5 + 0.5;
}

double fbm(double u, double v, Basis basis, int base_freq, int octaves,
           double gain, double lacunarity, std::uint32_t seed) {
    if (base_freq < 1) base_freq = 1;
    if (octaves   < 1) octaves   = 1;
    double sum = 0.0, amp = 1.0, norm = 0.0;
    int freq = base_freq;
    for (int o = 0; o < octaves; ++o) {
        const std::uint32_t s = seed + std::uint32_t(o) * 101u;
        const double n = (basis == Basis::Perlin) ? perlin_noise(u, v, freq, s)
                                                  : value_noise(u, v, freq, s);
        sum  += amp * n;
        norm += amp;
        amp  *= gain;
        freq  = int(std::lround(freq * lacunarity));
        if (freq < 1) freq = 1;
    }
    return norm > 0.0 ? sum / norm : 0.0;
}

} // namespace studio
```

- [ ] **Step 3: Write the failing test**

Create `tests/test_studio.cpp`:

```cpp
// =============================================================================
//  tests/test_studio.cpp  —  mini-studio core tests (dependency-free, CTest)
// =============================================================================
#include "games/studio/noise.hpp"

#include <cmath>
#include <cstdio>

using namespace studio;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool approx(double a, double b, double eps = 1e-9) {
    return std::fabs(a - b) <= eps * (1.0 + std::fabs(a) + std::fabs(b));
}

static void test_noise_range() {
    for (int i = 0; i < 50; ++i) {
        const double u = i * 0.017, v = i * 0.031;
        const double a = value_noise(u, v, 4, 7);
        const double b = perlin_noise(u, v, 4, 7);
        const double f = fbm(u, v, Basis::Perlin, 4, 5, 0.5, 2.0, 7);
        CHECK(a >= 0.0 && a <= 1.0);
        CHECK(b >= -1e-9 && b <= 1.0 + 1e-9);
        CHECK(f >= 0.0 && f <= 1.0);
    }
}

static void test_noise_deterministic() {
    CHECK(approx(value_noise(0.3, 0.4, 4, 7), value_noise(0.3, 0.4, 4, 7)));
    CHECK(!approx(value_noise(0.3, 0.4, 4, 7), value_noise(0.3, 0.4, 4, 8)));  // seed matters
}

static void test_noise_periodic() {
    // period 1.0 in u and v -> seamless tiling. Must hold for all three bases.
    CHECK(approx(value_noise(0.3, 0.4, 4, 7),  value_noise(1.3, 0.4, 4, 7)));
    CHECK(approx(value_noise(0.3, 0.4, 4, 7),  value_noise(0.3, 1.4, 4, 7)));
    CHECK(approx(perlin_noise(0.3, 0.4, 4, 7), perlin_noise(1.3, 0.4, 4, 7)));
    CHECK(approx(fbm(0.3, 0.4, Basis::Perlin, 4, 4, 0.5, 2.0, 7),
                 fbm(1.3, 0.4, Basis::Perlin, 4, 4, 0.5, 2.0, 7)));
    CHECK(approx(fbm(0.3, 0.4, Basis::Value, 3, 3, 0.5, 2.0, 9),
                 fbm(0.3, 1.4, Basis::Value, 3, 3, 0.5, 2.0, 9)));
}

int main() {
    test_noise_range();
    test_noise_deterministic();
    test_noise_periodic();
    if (g_failures == 0) std::printf("studio: all tests passed\n");
    else                 std::printf("studio: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
```

- [ ] **Step 4: Wire CMake**

In `CMakeLists.txt`, add after the `viz3d_core` block (near the other `*_core` libs):

```cmake
# ---- Mini Studio (Track B): procedural content authoring — PURE core (no SDL) ----
add_library(studio_core STATIC
  src/games/studio/noise.cpp
  src/games/studio/texture_gen.cpp
  src/games/studio/recipe.cpp
)
target_include_directories(studio_core PUBLIC src)
target_link_libraries(studio_core PRIVATE engine_flags)

# test_studio compiles image.cpp + assets.cpp directly so the encode/decode + recipe
# round-trips stay dependency-free (round-trips use in-memory buffers, no disk).
add_executable(test_studio tests/test_studio.cpp
  src/engine/image.cpp
  src/engine/assets.cpp
)
target_include_directories(test_studio PRIVATE src)
target_link_libraries(test_studio PRIVATE studio_core engine_flags)
add_test(NAME studio COMMAND test_studio)
```

> NOTE: `studio_core` lists `texture_gen.cpp` and `recipe.cpp` which don't exist yet (Tasks 2–3). Create empty-but-valid stubs now so the build configures, or add these two source lines in Task 2/3. Recommended: create the stub files in this step:
> - `src/games/studio/texture_gen.cpp` → `#include "games/studio/texture_gen.hpp"` won't compile without the header. Instead, for Step 4 only, temporarily list just `src/games/studio/noise.cpp` in `studio_core`, and add `texture_gen.cpp`/`recipe.cpp` to the `add_library` in Tasks 2 and 3. Use this temporary form now:

```cmake
add_library(studio_core STATIC
  src/games/studio/noise.cpp
)
```

- [ ] **Step 5: Configure + build + verify the test FAILS to compile/link, then passes**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --target test_studio`
Then: `ctest --test-dir build -R studio --output-on-failure`
Expected: `studio: all tests passed` (return 0).

- [ ] **Step 6: Commit**

```bash
git add src/games/studio/noise.hpp src/games/studio/noise.cpp tests/test_studio.cpp CMakeLists.txt
git commit -m "studio: seamless value/Perlin/fBm noise core + tests"
```

---

## Task 2: `encode_hrt` (close the image write-seam) + `texture_gen`

**Files:**
- Modify: `src/engine/image.hpp` (add `encode_hrt` decl), `src/engine/image.cpp` (impl)
- Create: `src/games/studio/texture_gen.hpp`, `src/games/studio/texture_gen.cpp`
- Modify: `tests/test_studio.cpp` (add tests), `CMakeLists.txt` (add `texture_gen.cpp` to `studio_core`)

- [ ] **Step 1: Add `encode_hrt` declaration**

In `src/engine/image.hpp`, after the `decode_hrt` declaration (line 29), add:

```cpp
// Encode an Image into .hrt bytes (inverse of decode_hrt): "HRT1" | BE w | BE h |
// R,G,B,A per pixel. Pure — the caller persists via assets::write_file.
std::vector<uint8_t> encode_hrt(const Image& img);
```

- [ ] **Step 2: Implement `encode_hrt`**

In `src/engine/image.cpp`, before `load_image` (after `decode_hrt`, line 45), add:

```cpp
std::vector<uint8_t> encode_hrt(const Image& img) {
    std::vector<uint8_t> out;
    if (img.w <= 0 || img.h <= 0) return out;
    out.reserve(12 + static_cast<size_t>(img.w) * img.h * 4);
    const char magic[4] = {'H', 'R', 'T', '1'};
    out.insert(out.end(), magic, magic + 4);
    auto be32 = [&](uint32_t v) {
        out.push_back(uint8_t(v >> 24)); out.push_back(uint8_t(v >> 16));
        out.push_back(uint8_t(v >> 8));  out.push_back(uint8_t(v));
    };
    be32(static_cast<uint32_t>(img.w));
    be32(static_cast<uint32_t>(img.h));
    for (Color c : img.pixels) {
        out.push_back(r_of(c)); out.push_back(g_of(c));
        out.push_back(b_of(c)); out.push_back(a_of(c));
    }
    return out;
}
```

- [ ] **Step 3: Create the texture_gen header**

Create `src/games/studio/texture_gen.hpp`:

```cpp
// =============================================================================
//  games/studio/texture_gen.hpp  —  parametric, tileable texture generator
// =============================================================================
#pragma once
#include <cstdint>

#include "engine/image.hpp"
#include "games/studio/noise.hpp"

namespace studio {

struct TextureParams {
    std::uint32_t seed       = 1;
    int           size       = 128;               // square, clamped >=8
    enum class Base { FBM, Value, Perlin, Checker, Wood } base = Base::FBM;
    Basis         basis      = Basis::Perlin;     // basis used by FBM / Wood
    int           frequency  = 4;                 // lattice cells across a tile (>=1)
    int           octaves    = 4;                 // FBM
    double        gain       = 0.5;
    double        lacunarity = 2.0;               // keep integer for perfect tiling
    gfx::Color    lo         = gfx::rgb(30, 24, 18);
    gfx::Color    hi         = gfx::rgb(210, 180, 140);
    enum class Op { None, Threshold, Contrast } op = Op::None;
    double        op_amount  = 0.5;
};

// Deterministic + pure: same params -> identical pixels. Always seamlessly tileable.
gfx::Image generate(const TextureParams& p);

} // namespace studio
```

- [ ] **Step 4: Create the texture_gen implementation**

Create `src/games/studio/texture_gen.cpp`:

```cpp
// =============================================================================
//  games/studio/texture_gen.cpp
// =============================================================================
#include "games/studio/texture_gen.hpp"

#include <algorithm>
#include <cmath>

namespace studio {
namespace {

uint8_t lerp8(uint8_t a, uint8_t b, double t) {
    return uint8_t(std::clamp(a + (double(b) - a) * t, 0.0, 255.0) + 0.5);
}
gfx::Color ramp(gfx::Color lo, gfx::Color hi, double t) {
    t = std::clamp(t, 0.0, 1.0);
    return gfx::rgb(lerp8(gfx::r_of(lo), gfx::r_of(hi), t),
                    lerp8(gfx::g_of(lo), gfx::g_of(hi), t),
                    lerp8(gfx::b_of(lo), gfx::b_of(hi), t));
}
double apply_op(double t, const TextureParams& p) {
    switch (p.op) {
        case TextureParams::Op::Threshold: return t >= p.op_amount ? 1.0 : 0.0;
        case TextureParams::Op::Contrast: {
            const double k = 0.5 + p.op_amount * 3.0;               // slope 0.5..3.5
            return std::clamp((t - 0.5) * k + 0.5, 0.0, 1.0);
        }
        default: return t;
    }
}
double sample(const TextureParams& p, double u, double v) {
    switch (p.base) {
        case TextureParams::Base::Value:  return value_noise(u, v, p.frequency, p.seed);
        case TextureParams::Base::Perlin: return perlin_noise(u, v, p.frequency, p.seed);
        case TextureParams::Base::Checker: {
            const int cx = int(u * p.frequency), cy = int(v * p.frequency);
            return ((cx + cy) & 1) ? 1.0 : 0.0;                    // tiles if frequency is even
        }
        case TextureParams::Base::Wood: {
            const double n = fbm(u, v, p.basis, p.frequency, p.octaves, p.gain, p.lacunarity, p.seed);
            const double rings = std::sin((u + n * 0.5) * p.frequency * 6.28318530718);
            return rings * 0.5 + 0.5;
        }
        case TextureParams::Base::FBM:
        default: return fbm(u, v, p.basis, p.frequency, p.octaves, p.gain, p.lacunarity, p.seed);
    }
}

} // namespace

gfx::Image generate(const TextureParams& in) {
    TextureParams p = in;
    if (p.size < 8) p.size = 8;
    if (p.frequency < 1) p.frequency = 1;
    gfx::Image im;
    im.w = p.size; im.h = p.size;
    im.pixels.resize(static_cast<size_t>(p.size) * p.size);
    for (int y = 0; y < p.size; ++y) {
        const double v = double(y) / p.size;
        for (int x = 0; x < p.size; ++x) {
            const double u = double(x) / p.size;
            const double t = apply_op(sample(p, u, v), p);
            im.pixels[static_cast<size_t>(y) * p.size + x] = ramp(p.lo, p.hi, t);
        }
    }
    return im;
}

} // namespace studio
```

- [ ] **Step 5: Add `texture_gen.cpp` to the build**

In `CMakeLists.txt`, change the `studio_core` sources to:

```cmake
add_library(studio_core STATIC
  src/games/studio/noise.cpp
  src/games/studio/texture_gen.cpp
)
```

- [ ] **Step 6: Add tests (append to `tests/test_studio.cpp`)**

Add these includes at the top (after the noise include):

```cpp
#include "games/studio/texture_gen.hpp"
#include "engine/image.hpp"
```

Add these functions before `main()`:

```cpp
static void test_texture_deterministic() {
    TextureParams p;                       // defaults: FBM/Perlin, 128, freq 4
    const gfx::Image a = generate(p);
    const gfx::Image b = generate(p);
    CHECK(a.w == 128 && a.h == 128);
    CHECK(a.pixels == b.pixels);           // byte-identical
    TextureParams q = p; q.seed = 2;
    CHECK(!(generate(q).pixels == a.pixels));   // seed changes output
}

static void test_texture_size_clamp() {
    TextureParams p; p.size = 2; p.frequency = 0;
    const gfx::Image im = generate(p);
    CHECK(im.w == 8 && im.h == 8);          // size clamped to 8, freq clamped to 1
}

static void test_hrt_roundtrip() {
    TextureParams p; p.size = 32;
    const gfx::Image im = generate(p);
    const std::vector<uint8_t> bytes = gfx::encode_hrt(im);
    CHECK(bytes.size() == size_t(12 + 32 * 32 * 4));
    const auto back = gfx::decode_hrt(bytes);
    CHECK(back.has_value());
    CHECK(back->w == im.w && back->h == im.h);
    CHECK(back->pixels == im.pixels);       // exact round-trip
}
```

Add the calls inside `main()` (before the summary printf):

```cpp
    test_texture_deterministic();
    test_texture_size_clamp();
    test_hrt_roundtrip();
```

- [ ] **Step 7: Build + test**

Run: `cmake --build build --target test_studio && ctest --test-dir build -R studio --output-on-failure`
Expected: `studio: all tests passed`.

- [ ] **Step 8: Commit**

```bash
git add src/engine/image.hpp src/engine/image.cpp \
        src/games/studio/texture_gen.hpp src/games/studio/texture_gen.cpp \
        tests/test_studio.cpp CMakeLists.txt
git commit -m "studio: encode_hrt (image write-seam) + parametric texture_gen + tests"
```

---

## Task 3: Recipe (re-editable text sidecar)

**Files:**
- Create: `src/games/studio/recipe.hpp`, `src/games/studio/recipe.cpp`
- Modify: `tests/test_studio.cpp`, `CMakeLists.txt` (add `recipe.cpp` to `studio_core`)

- [ ] **Step 1: Create the header**

Create `src/games/studio/recipe.hpp`:

```cpp
// =============================================================================
//  games/studio/recipe.hpp  —  TextureParams <-> key=value text (non-destructive)
// =============================================================================
//  A texture is saved with a tiny `.recipe` sidecar so it can be RE-EDITED later:
//  we reload the exact params, not just the flat pixels. Plain key=value text,
//  hand-parsed (no JSON lib) — upgrade to sdk json.hpp only if nesting appears.
// =============================================================================
#pragma once
#include <string>

#include "games/studio/texture_gen.hpp"

namespace studio {

std::string   to_recipe(const TextureParams& p);       // deterministic key=value dump
TextureParams from_recipe(const std::string& text);    // missing/unknown keys -> defaults

} // namespace studio
```

- [ ] **Step 2: Create the implementation**

Create `src/games/studio/recipe.cpp`:

```cpp
// =============================================================================
//  games/studio/recipe.cpp
// =============================================================================
#include "games/studio/recipe.hpp"

#include <sstream>

namespace studio {

std::string to_recipe(const TextureParams& p) {
    std::ostringstream o;
    o << "seed="       << p.seed                     << '\n'
      << "size="       << p.size                     << '\n'
      << "base="       << int(p.base)                << '\n'
      << "basis="      << int(p.basis)               << '\n'
      << "frequency="  << p.frequency                << '\n'
      << "octaves="    << p.octaves                  << '\n'
      << "gain="       << p.gain                     << '\n'
      << "lacunarity=" << p.lacunarity               << '\n'
      << "lo="         << static_cast<unsigned long>(p.lo) << '\n'
      << "hi="         << static_cast<unsigned long>(p.hi) << '\n'
      << "op="         << int(p.op)                  << '\n'
      << "op_amount="  << p.op_amount                << '\n';
    return o.str();
}

TextureParams from_recipe(const std::string& text) {
    TextureParams p;                       // start from defaults; override what we parse
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        try {
            if      (key == "seed")       p.seed       = std::uint32_t(std::stoul(val));
            else if (key == "size")       p.size       = std::stoi(val);
            else if (key == "base")       p.base       = TextureParams::Base(std::stoi(val));
            else if (key == "basis")      p.basis      = Basis(std::stoi(val));
            else if (key == "frequency")  p.frequency  = std::stoi(val);
            else if (key == "octaves")    p.octaves    = std::stoi(val);
            else if (key == "gain")       p.gain       = std::stod(val);
            else if (key == "lacunarity") p.lacunarity = std::stod(val);
            else if (key == "lo")         p.lo         = gfx::Color(std::stoul(val));
            else if (key == "hi")         p.hi         = gfx::Color(std::stoul(val));
            else if (key == "op")         p.op         = TextureParams::Op(std::stoi(val));
            else if (key == "op_amount")  p.op_amount  = std::stod(val);
        } catch (...) { /* malformed value -> keep the default for that key */ }
    }
    return p;
}

} // namespace studio
```

- [ ] **Step 3: Add `recipe.cpp` to the build**

In `CMakeLists.txt`, update `studio_core` to its final form:

```cmake
add_library(studio_core STATIC
  src/games/studio/noise.cpp
  src/games/studio/texture_gen.cpp
  src/games/studio/recipe.cpp
)
```

- [ ] **Step 4: Add the round-trip test**

In `tests/test_studio.cpp`, add the include (after the texture_gen include):

```cpp
#include "games/studio/recipe.hpp"
```

Add before `main()`:

```cpp
static void test_recipe_roundtrip() {
    TextureParams p;
    p.seed = 42; p.size = 64; p.base = TextureParams::Base::Wood;
    p.basis = Basis::Value; p.frequency = 6; p.octaves = 3;
    p.gain = 0.6; p.lacunarity = 2.0; p.lo = gfx::rgb(10, 20, 30);
    p.hi = gfx::rgb(200, 190, 180); p.op = TextureParams::Op::Contrast; p.op_amount = 0.7;
    // Re-serializing the parsed recipe must reproduce the original text exactly.
    const std::string once = to_recipe(p);
    CHECK(to_recipe(from_recipe(once)) == once);
    // And the parsed params must regenerate identical pixels.
    CHECK(generate(from_recipe(once)).pixels == generate(p).pixels);
}
```

Add the call in `main()`:

```cpp
    test_recipe_roundtrip();
```

- [ ] **Step 5: Build + test**

Run: `cmake --build build --target test_studio && ctest --test-dir build -R studio --output-on-failure`
Expected: `studio: all tests passed`.

- [ ] **Step 6: Commit**

```bash
git add src/games/studio/recipe.hpp src/games/studio/recipe.cpp tests/test_studio.cpp CMakeLists.txt
git commit -m "studio: re-editable recipe sidecar (params <-> text) + round-trip test"
```

---

## Task 4: The `--studio` scene (preview, params panel, save, collection)

This task is UI/integration — verified by running it, not unit tests (the generator core it drives is already fully tested). Code is complete below; model is `editor_scene.cpp`.

**Files:**
- Create: `src/games/studio/studio_scene.hpp`, `src/games/studio/studio_scene.cpp`
- Modify: `CMakeLists.txt` (add `studio_scene.cpp` to `demo`; link `studio_core`), `src/main.cpp` (`--studio`)

- [ ] **Step 1: Create the scene header**

Create `src/games/studio/studio_scene.hpp`:

```cpp
// =============================================================================
//  games/studio/studio_scene.hpp  —  the Texture Lab (--studio)
// =============================================================================
#pragma once
#include <string>
#include <vector>

#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "games/studio/texture_gen.hpp"

namespace studio {

class StudioScene : public engine::Scene {
public:
    StudioScene();
    void render(const engine::Context& ctx) override;

private:
    void sync_params();      // mirror slider/cycle state -> params_
    void regenerate();       // params_ -> preview_
    void save_current();     // encode + write .hrt + .recipe, add to collection_
    void load_saved(const std::string& name);   // read .recipe -> params_

    TextureParams            params_;
    gfx::Image               preview_;
    bool                     dirty_ = true;
    ui::Context              ui_;

    // slider/cycle mirror state
    float freq_f_ = 4, oct_f_ = 4, gain_f_ = 0.5f, lac_f_ = 2, opamt_f_ = 0.5f;
    int   base_idx_ = 0, op_idx_ = 0, ramp_idx_ = 0;
    std::uint32_t seed_ = 1;

    int save_counter_ = 0;
    std::vector<std::string> collection_;    // saved texture names (this session)
    int w_ = 0, h_ = 0;
};

} // namespace studio
```

- [ ] **Step 2: Create the scene implementation**

Create `src/games/studio/studio_scene.cpp`:

```cpp
// =============================================================================
//  games/studio/studio_scene.cpp
// =============================================================================
#include "games/studio/studio_scene.hpp"

#include <cstdio>

#include "engine/assets.hpp"
#include "engine/color.hpp"
#include "engine/image.hpp"
#include "engine/ui/theme.hpp"
#include "games/studio/recipe.hpp"

namespace studio {

using platform::MouseButton;

namespace {
struct Ramp { const char* name; gfx::Color lo, hi; };
const Ramp kRamps[] = {
    {"sand",  gfx::rgb(120, 96, 60),  gfx::rgb(224, 200, 150)},
    {"stone", gfx::rgb(40, 42, 48),   gfx::rgb(190, 195, 205)},
    {"grass", gfx::rgb(20, 60, 24),   gfx::rgb(120, 190, 90)},
    {"lava",  gfx::rgb(40, 8, 4),     gfx::rgb(250, 180, 60)},
};
constexpr int kRampCount = int(sizeof(kRamps) / sizeof(kRamps[0]));
const char* kBaseNames[] = {"FBM", "Value", "Perlin", "Checker", "Wood"};
const char* kOpNames[]   = {"None", "Threshold", "Contrast"};
} // namespace

StudioScene::StudioScene() { regenerate(); }

void StudioScene::sync_params() {
    params_.frequency  = freq_f_  < 1 ? 1 : int(freq_f_ + 0.5f);
    params_.octaves    = oct_f_   < 1 ? 1 : int(oct_f_ + 0.5f);
    params_.gain       = gain_f_;
    params_.lacunarity = lac_f_;
    params_.op_amount  = opamt_f_;
    params_.seed       = seed_;
    params_.base       = TextureParams::Base(base_idx_);
    params_.op         = TextureParams::Op(op_idx_);
    params_.lo         = kRamps[ramp_idx_].lo;
    params_.hi         = kRamps[ramp_idx_].hi;
}

void StudioScene::regenerate() { sync_params(); preview_ = generate(params_); }

void StudioScene::save_current() {
    char name[32];
    std::snprintf(name, sizeof(name), "studio_%02d", save_counter_++);
    const std::vector<uint8_t> hrt = gfx::encode_hrt(preview_);
    assets::write_file(std::string("textures/") + name + ".hrt", hrt);
    const std::string rec = to_recipe(params_);
    assets::write_file(std::string("textures/") + name + ".recipe",
                       std::vector<uint8_t>(rec.begin(), rec.end()));
    collection_.push_back(name);
}

void StudioScene::load_saved(const std::string& name) {
    auto bytes = assets::load_file(std::string("textures/") + name + ".recipe");
    if (!bytes) return;
    params_ = from_recipe(std::string(bytes->begin(), bytes->end()));
    freq_f_ = float(params_.frequency); oct_f_ = float(params_.octaves);
    gain_f_ = float(params_.gain);      lac_f_ = float(params_.lacunarity);
    opamt_f_ = float(params_.op_amount); seed_ = params_.seed;
    base_idx_ = int(params_.base); op_idx_ = int(params_.op);
    dirty_ = true;
}

void StudioScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width(); h_ = g.height();
    g.set_font(ctx.font, ui::theme::sz_body);
    g.clear(gfx::rgb(24, 26, 34));

    // ---- live preview (blit the current image; 2x2 to show it tiles) ----
    const int ps = preview_.w;                     // 128
    const int px = 260, py = 60;
    gfx::Sprite spr{preview_.pixels.data(), preview_.w, preview_.h};
    for (int ty = 0; ty < 2; ++ty)
        for (int tx = 0; tx < 2; ++tx)
            g.blit(spr, px + tx * ps, py + ty * ps);
    g.draw_rect(px, py, ps * 2, ps * 2, gfx::rgb(70, 74, 86));
    g.draw_text(px, py - 18, "preview (2x2 tiled)", ui::theme::text_muted);

    // ---- params panel (immediate mode) ----
    ui::Input in;
    in.mx = ctx.input.mouse_x; in.my = ctx.input.mouse_y;
    in.down     = ctx.input.down(MouseButton::Left);
    in.pressed  = ctx.input.pressed(MouseButton::Left);
    in.released = ctx.input.released(MouseButton::Left);

    ui_.begin(&g, in);
    ui_.panel(ui::Rect{12, 12, 232, 300}, "TEXTURE LAB");
    char buf[48];
    std::snprintf(buf, sizeof(buf), "base: %s", kBaseNames[base_idx_]);
    if (ui_.button(buf)) { base_idx_ = (base_idx_ + 1) % 5; dirty_ = true; }
    std::snprintf(buf, sizeof(buf), "op: %s", kOpNames[op_idx_]);
    if (ui_.button(buf)) { op_idx_ = (op_idx_ + 1) % 3; dirty_ = true; }
    std::snprintf(buf, sizeof(buf), "ramp: %s", kRamps[ramp_idx_].name);
    if (ui_.button(buf)) { ramp_idx_ = (ramp_idx_ + 1) % kRampCount; dirty_ = true; }
    if (ui_.slider("frequency", freq_f_, 1, 32))    dirty_ = true;
    if (ui_.slider("octaves",   oct_f_, 1, 8))      dirty_ = true;
    if (ui_.slider("gain",      gain_f_, 0.1f, 0.9f)) dirty_ = true;
    if (ui_.slider("lacunarity",lac_f_, 2, 4))      dirty_ = true;
    if (ui_.slider("op amount", opamt_f_, 0, 1))    dirty_ = true;
    if (ui_.button("Randomize seed")) { seed_ = seed_ * 1664525u + 1013904223u; dirty_ = true; }
    if (ui_.button("Save", true))     save_current();
    ui_.end();

    // ---- collection browser (loads a saved recipe back for re-editing) ----
    ui_.begin(&g, in);
    ui_.panel(ui::Rect{w_ - 180, 12, 168, 300}, "COLLECTION");
    for (size_t i = 0; i < collection_.size(); ++i) {
        char lbl[40];
        std::snprintf(lbl, sizeof(lbl), "%s##%zu", collection_[i].c_str(), i);  // unique id
        if (ui_.button(lbl)) load_saved(collection_[i]);
    }
    ui_.end();

    if (dirty_) { regenerate(); dirty_ = false; }

    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(12, h_ - 22,
                "cycle base/op/ramp - drag sliders - Save writes assets/textures/*.hrt - ESC quits",
                ui::theme::text_muted);
}

} // namespace studio
```

> NOTE on the `##` label suffix: the `ui` widget id is a hash of the whole label string (see `ui.hpp` contract), so `name##0`, `name##1` are distinct ids. The visible text still shows the `##` — acceptable for v1; a label/id split is future polish.

- [ ] **Step 3: Wire the scene into the build**

In `CMakeLists.txt`, add `src/games/studio/studio_scene.cpp` to the `add_executable(demo ...)` source list (after the other game scenes), and add `studio_core` to the demo link line:

```cmake
  src/games/editor/editor_scene.cpp
  src/games/colony/colony_scene.cpp
  src/games/studio/studio_scene.cpp
)
```
```cmake
target_link_libraries(demo PRIVATE ${SDL2_LINK} engine_flags chess_core fps_core render3d_core viz3d_core ui_core text_core physics_core colony_core mem_core gbaas_sdk studio_core)
```

- [ ] **Step 4: Add the `--studio` dispatch**

In `src/main.cpp`, add the include (with the other game includes):

```cpp
#include "games/studio/studio_scene.hpp"
```

Add this branch before the final no-args demo (after the `--colony` block):

```cpp
    if (mode == "--studio") {
        platform::Config cfg;
        cfg.title     = "hand-engine — texture lab";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<studio::StudioScene>());
    }
```

- [ ] **Step 5: Build the whole demo + run it**

Run: `cmake --build build --target demo`
Expected: links clean (no SDL leak, `-Wall -Wextra -Wpedantic` quiet).
Run: `./build/demo --studio`
Expected: a window with a live-updating 2×2-tiled texture preview; cycling base/op/ramp and dragging sliders changes it; **Save** writes `assets/textures/studio_00.hrt` + `.recipe` and adds an entry to the COLLECTION panel; clicking that entry reloads its params.
Verify the save wrote files: `ls assets/textures/` → shows `studio_00.hrt studio_00.recipe`.

- [ ] **Step 6: Commit**

```bash
git add src/games/studio/studio_scene.hpp src/games/studio/studio_scene.cpp CMakeLists.txt src/main.cpp
git commit -m "studio: --studio Texture Lab scene (live preview + save + collection)"
```

---

## Task 5: Prove the loop — load a studio texture in a scene; sanitizer + merge

**Files:**
- Modify: `docs/book/` (new chapters 73–75), `README.md` (roadmap row + `--studio` in the run list)

- [ ] **Step 1: Manual end-to-end check via the asset cache**

The engine already loads `.hrt` through `gfx::load_image` / the asset cache. Confirm a studio-made texture loads: in a scratch check or an existing scene, `gfx::load_image("textures/studio_00.hrt")` returns a populated `Image` (w/h 128, non-empty pixels). This closes produce→save→consume. (No new test target — `test_studio` already covers encode/decode; this is a runtime confirmation.)

Run: `./build/demo --studio` → Save, then confirm `assets/textures/studio_00.hrt` exists and `decode` succeeds (the round-trip test already asserts decode correctness).

- [ ] **Step 2: Sanitizer build (memory/UB gate)**

Run: `cmake -B build-asan -DENGINE_SANITIZE=ON && cmake --build build-asan --target test_studio && ctest --test-dir build-asan -R studio --output-on-failure`
Expected: `studio: all tests passed`, no ASan/UBSan reports.

- [ ] **Step 3: Update README**

In `README.md`, add to the run list (after `--editor`/`--colony`):

```
./build/demo --studio   # Mini Studio: procedural Texture Lab (cycle base/op/ramp, sliders, Save)
```
Add a roadmap row: `| Studio v1 | Mini Studio — procedural Texture Lab (noise, seamless tiling, .hrt export + recipes) | ✅ done |`.

- [ ] **Step 4: Write guidebook chapters (thorough, per working style — split small)**

Create `docs/book/73-procedural-noise.md` (value → Perlin → fBm: theory, ASCII lattice diagram, worked numbers, pitfalls, exercises), `docs/book/74-seamless-tiling.md` (period-1 torus wrap; why edge-equality is the wrong test and periodicity is the right invariant), `docs/book/75-texture-lab-studio.md` (params, live preview, encode_hrt, recipes, collection). Match the depth of existing chapters.

- [ ] **Step 5: Commit docs**

```bash
git add README.md docs/book/73-procedural-noise.md docs/book/74-seamless-tiling.md docs/book/75-texture-lab-studio.md
git commit -m "studio: guidebook ch.73-75 + README run/roadmap for the Texture Lab"
```

- [ ] **Step 6: Full suite + merge to main**

Run: `ctest --test-dir build --output-on-failure` (all suites, incl. `studio`, green).
Then merge the branch (local only — do NOT push):

```bash
git checkout main
git merge --no-ff feat/studio-texture-lab -m "Merge Mini Studio v1: procedural Texture Lab"
git branch -d feat/studio-texture-lab
```

---

## Self-Review

**Spec coverage** (against `2026-07-11-platform-expansion-and-mini-studio-design.md` §2):
- §2.2 architecture (studio_core + scene + image seam) → Tasks 1–4. ✓
- §2.3 noise (value/Perlin/fBm, tileable) → Task 1. ✓
- §2.3 base patterns + operators + ramp → Task 2 (`texture_gen`). ✓
- §2.4 `encode_hrt` + recipe sidecar → Task 2 + Task 3. ✓
- §2.5 UI panel + throttled regen + collection → Task 4 (`dirty_` gate; collection panel). ✓
- §2.7 tests (determinism, seamlessness, round-trips) → Tasks 1–3. ✓ (seamlessness tested as noise **periodicity**, the correct invariant — see below).
- §2.8 CMake (`studio_core`, `test_studio`, demo wiring) → Tasks 1–4. ✓
- §2.9 integration → Task 5. ✓
- §2.10 guidebook ch.73–75 → Task 5. ✓
- §2.6 web (WASM) is preview-only / no IDBFS → intentionally out of scope; collection is in-session only. ✓

**Correction vs spec:** the spec §2.7 phrased seamlessness as "edge column 0 == column w-1". That is imprecise — the last rendered column is `u=(w-1)/w`, not `u=1`. The correct, tested invariant is **noise periodicity**: `noise(u,v)==noise(u+1,v)` (period 1.0), which guarantees tile continuity. Task 1's `test_noise_periodic` uses this. The spec's §2.7 bullet is updated to match.

**Placeholder scan:** no TBD/TODO; every code step shows complete code; every command has an expected result. ✓

**Type consistency:** `TextureParams`, `Basis`, `generate`, `encode_hrt`, `to_recipe`/`from_recipe`, `value_noise`/`perlin_noise`/`fbm` signatures are identical across the header, impl, tests, recipe, and scene. Enum orders (`Base{FBM,Value,Perlin,Checker,Wood}`, `Op{None,Threshold,Contrast}`) match `base_idx_`/`op_idx_` cycling and `int(enum)` recipe encoding. ✓

**Ponytail notes (deliberate simplifications, upgrade paths):** in-session collection (directory scan later) · preset ramps instead of a full color picker (RGB sliders later) · `##` shown in labels (id/label split later) · web persistence deferred (IDBFS later).
