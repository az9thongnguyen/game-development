// =============================================================================
//  engine/bench.hpp  —  what one frame costs, as data
// =============================================================================
//  `--bench-ui` has existed since chapter 108 and lived entirely inside `src/main.cpp`:
//  the warm-up count, the percentile arithmetic, the 8 ms verdict and the printing, all
//  in one sixty-line block behind a flag. That is the shape this repo forbids everywhere
//  else — the operation belongs in a pure core and the trigger only calls it — and the
//  cost of the exception was exactly what the rule predicts:
//
//    * `--bench-ui 0` SEGFAULTED. `ms[ms.size() / 2]` on an empty vector. Nothing could
//      ask the percentile code a question without also allocating a 14 MB framebuffer
//      and rendering the Studio, so nobody ever asked it one.
//    * it could only ever measure the STUDIO. `PROJECT-BRIEF` has carried the warning
//      "measures the Studio with no game running" since chapter 117, and the two games
//      that draw an on-screen control pad every frame at ss=2 arrived after it.
//
//  Split in two, because the two halves fail differently: `summarize` is arithmetic and
//  can be wrong in a way no timing test would ever catch, and `run` is a loop whose only
//  honest test is that it produces the number of samples it was asked for.
// =============================================================================
#pragma once

#include <vector>

#include "engine/scene.hpp"

namespace text { class Font; }

namespace bench {

struct Summary {
    int    frames = 0;
    double median = 0, p95 = 0, best = 0, worst = 0;
};

// Nearest-rank: the returned value is a frame that actually happened, not an
// interpolation between two that did. `p` is 0..1; a sorted, non-empty input.
[[nodiscard]] double percentile(const std::vector<double>& sorted_ms, double p);

// The statistics. Takes the samples by value and sorts them, so a caller cannot be
// surprised by its own vector coming back reordered.
//
// An EMPTY run is a Summary of zeros, not a crash and not a lie: `frames = 0` is the
// honest report of "you asked for no frames", and every caller that prints a verdict
// has to look at it. This is the whole reason the function exists apart from the loop.
[[nodiscard]] Summary summarize(std::vector<double> ms);

struct Config {
    int lw = 1280, lh = 720;   // LOGICAL size; the framebuffer is this times `ss`
    int ss = 1;                // supersample factor — costs ss*ss pixels
    int frames = 120;
    int warmup = 20;           // untimed: first-touch page faults on a fresh
                               // framebuffer, and each type size rasterized once
};

// Render `scene` into a framebuffer of its own, `frames` times, and report. RENDER
// only: no update(), so this is the rasterization cost and not the simulation's — which
// is what the question "does this fit in a frame at ss=2" is actually about, and it
// keeps the numbers comparable with every one taken since chapter 108.
[[nodiscard]] Summary run(engine::Scene& scene, const Config& cfg, text::Font* font);

}  // namespace bench
