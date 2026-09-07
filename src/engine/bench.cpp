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
    if (p <= 0.0) return sorted_ms.front();
    if (p >= 1.0) return sorted_ms.back();
    const double n    = static_cast<double>(sorted_ms.size());
    // Nearest rank: ceil(p * n), 1-based. The old code was `size * 95 / 100`, integer
    // arithmetic on the INDEX — which for ten samples asks for index 9, the worst frame
    // of the run reported as its 95th percentile.
    auto rank = static_cast<std::size_t>(std::ceil(p * n));
    if (rank < 1) rank = 1;
    if (rank > sorted_ms.size()) rank = sorted_ms.size();
    return sorted_ms[rank - 1];
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
