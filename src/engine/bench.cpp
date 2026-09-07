// =============================================================================
//  engine/bench.cpp  —  see bench.hpp
// =============================================================================
#include "engine/bench.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

#include "engine/renderer2d.hpp"
#include "platform/input.hpp"

namespace bench {

double percentile(const std::vector<double>& sorted_ms, double p) {
    if (sorted_ms.empty()) return 0.0;
    const auto n = static_cast<long long>(sorted_ms.size());
    // Nearest rank: ceil(p * n), 1-based. The old code in main.cpp was
    // `size * 95 / 100` — integer arithmetic on the INDEX, which for ten samples asks
    // for index 9: the worst frame of the run, reported as its 95th percentile.
    //
    // Computed SIGNED and then clamped. This function was first written with early-outs
    // for `p <= 0` and `p >= 1` as well, and four mutations survived because of them:
    // the early-outs and the clamps each cover the other exactly, so removing either
    // one never changes an answer. Four survivors were not four missing tests, they
    // were four redundant lines. The clamps stay (they are the ones an out-of-range `p`
    // needs), and signed arithmetic is what makes them load-bearing — casting a
    // negative rank to size_t would wrap it into a very large index instead.
    long long rank = static_cast<long long>(std::ceil(p * static_cast<double>(n)));
    if (rank < 1) rank = 1;
    if (rank > n) rank = n;
    return sorted_ms[static_cast<std::size_t>(rank - 1)];
}

Summary summarize(std::vector<double> ms) {
    Summary s;
    s.frames = static_cast<int>(ms.size());
    if (ms.empty()) return s;      // zeros, and frames == 0 says why
    std::sort(ms.begin(), ms.end());
    s.best   = ms.front();
    s.worst  = ms.back();
    s.median = percentile(ms, 0.50);
    s.p95    = percentile(ms, 0.95);
    return s;
}

Summary run(engine::Scene& scene, const Config& cfg, text::Font* font) {
    if (cfg.lw <= 0 || cfg.lh <= 0 || cfg.ss <= 0) return Summary{};
    const int pw = cfg.lw * cfg.ss, ph = cfg.lh * cfg.ss;
    std::vector<std::uint32_t> buf(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph), 0);
    platform::Framebuffer fb{buf.data(), pw, ph, pw};
    platform::InputState  in{};

    for (int i = 0; i < cfg.warmup; ++i) {
        gfx::Renderer2D r(fb, cfg.ss);
        const engine::Context ctx{r, in, 1.0 / 60.0, 0.0, 0.0, font};
        scene.render(ctx);
    }

    std::vector<double> ms;
    ms.reserve(static_cast<std::size_t>(std::max(0, cfg.frames)));
    for (int i = 0; i < cfg.frames; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        gfx::Renderer2D r(fb, cfg.ss);
        const engine::Context ctx{r, in, 1.0 / 60.0, 0.0, 0.0, font};
        scene.render(ctx);
        const auto t1 = std::chrono::steady_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return summarize(std::move(ms));
}

}  // namespace bench
