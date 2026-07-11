// =============================================================================
//  tests/test_aa.cpp  —  anti-aliasing: SSAA coordinate scaling + AA primitives
// =============================================================================
//  No SDL: draw into a plain offscreen framebuffer and inspect pixels.
//  Phase 2.1 covers the SSAA seam (logical→physical scaling); 2.2/2.3 extend this
//  file with Xiaolin Wu lines and coverage-based rounded rects / circles.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/renderer2d.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

int main() {
    using gfx::Renderer2D;
    constexpr std::uint32_t BG = 0xFF000000, FG = 0xFFFFFFFF;

    // --- ss=1: logical == physical (regression guard) ---
    {
        constexpr int W = 8, H = 8;
        std::vector<std::uint32_t> buf(W * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        Renderer2D r(fb, 1);
        CHECK(r.width() == 8 && r.height() == 8 && r.supersample() == 1);
        r.fill_rect(2, 3, 2, 1, FG);
        CHECK(buf[3 * W + 2] == FG && buf[3 * W + 3] == FG);
        CHECK(buf[3 * W + 4] == BG && buf[2 * W + 2] == BG);
    }

    // --- ss=2: logical size halved; a 1x1 logical fill = a 2x2 physical block ---
    {
        constexpr int W = 8, H = 8;
        std::vector<std::uint32_t> buf(W * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        Renderer2D r(fb, 2);
        CHECK(r.width() == 4 && r.height() == 4 && r.supersample() == 2);

        r.fill_rect(0, 0, 1, 1, FG);
        CHECK(buf[0] == FG && buf[1] == FG && buf[W + 0] == FG && buf[W + 1] == FG);  // 2x2
        CHECK(buf[2] == BG && buf[2 * W] == BG);                                       // no bleed

        r.set_pixel(1, 1, FG);                        // logical (1,1) → physical (2,2)-(3,3)
        CHECK(buf[2 * W + 2] == FG && buf[3 * W + 3] == FG);
        CHECK(buf[2 * W + 4] == BG);
    }

    // --- draw_rect: a 1px logical outline is ss px thick physically, hole empty ---
    {
        constexpr int W = 8, H = 8;
        std::vector<std::uint32_t> buf(W * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        Renderer2D r(fb, 2);
        r.draw_rect(0, 0, 4, 4, FG);                  // logical 4x4 → 8x8 physical, 2px border
        CHECK(buf[0] == FG && buf[1] == FG && buf[W] == FG && buf[W + 1] == FG);  // corner block
        CHECK(buf[3 * W + 3] == BG);                  // interior hole
    }

    // --- Wu AA line: horizontal is crisp; a shallow diagonal splits coverage ---
    {
        constexpr int W = 12, H = 8;
        std::vector<std::uint32_t> buf(W * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        Renderer2D r(fb, 1);
        auto R = [&](int x, int y) { return (buf[y * W + x] >> 16) & 0xFF; };

        r.draw_line_aa(1, 4, 6, 4, FG);              // horizontal
        CHECK(R(3, 4) > 200);                        // on the line: near-full coverage
        CHECK(R(3, 3) == 0 && R(3, 5) == 0);         // neighbours untouched

        for (auto& p : buf) p = BG;
        r.draw_line_aa(0, 0, 8, 3, FG);              // shallow diagonal (grad 0.375)
        const int a = R(4, 1), b = R(4, 2);
        CHECK(a > 0 && b > 0);                        // both straddling pixels lit...
        CHECK(a + b > 200 && a + b < 300);           // ...and coverage sums to ~1 (that's AA)
    }

    // --- coverage shapes: rounded rect corners are AA and monotonic ---
    {
        constexpr int W = 12, H = 12;
        std::vector<std::uint32_t> buf(W * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        Renderer2D r(fb, 1);
        auto R = [&](int x, int y) { return int((buf[y * W + x] >> 16) & 0xFF); };

        r.fill_round_rect(0, 0, 10, 10, 3, FG);
        CHECK(R(5, 5) == 255);                       // interior solid
        CHECK(R(0, 0) == 0);                          // corner rounded away
        const int e = R(1, 0);
        CHECK(e > 0 && e < 255);                      // arc pixel is fractional (AA)
        CHECK(R(2, 0) >= e && e >= R(0, 0));          // coverage decreases outward
    }

    // --- filled circle: solid centre, empty far corner, AA edge exists ---
    {
        constexpr int W = 14, H = 14;
        std::vector<std::uint32_t> buf(W * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        Renderer2D r(fb, 1);
        auto R = [&](int x, int y) { return int((buf[y * W + x] >> 16) & 0xFF); };

        r.fill_circle(6, 6, 4, FG);
        CHECK(R(6, 6) == 255);                        // centre solid
        CHECK(R(0, 0) == 0);                          // far corner empty
        int frac = 0;
        for (auto p : buf) { const int rr = (p >> 16) & 0xFF; if (rr > 0 && rr < 255) ++frac; }
        CHECK(frac > 0);                              // an anti-aliased edge exists
    }

    // --- drop shadow: darkens pixels around the rect but stays translucent ---
    {
        constexpr int W = 20, H = 20;
        constexpr std::uint32_t GREY = 0xFF888888;
        std::vector<std::uint32_t> buf(W * H, GREY);
        platform::Framebuffer fb{buf.data(), W, H, W};
        Renderer2D r(fb, 1);
        auto R = [&](int x, int y) { return int((buf[y * W + x] >> 16) & 0xFF); };

        r.drop_shadow(6, 6, 8, 5, 2, /*dx*/0, /*dy*/2, /*spread*/3, 0xFF000000);
        int partial = 0;
        for (auto p : buf) { const int rr = (p >> 16) & 0xFF; if (rr > 0 && rr < 0x88) ++partial; }
        CHECK(partial > 0);                          // a soft, translucent penumbra exists
        CHECK(R(19, 0) == 0x88);                      // far corner untouched
    }

    // --- vertical gradient: top≈top colour, bottom≈bottom colour, monotonic between ---
    {
        constexpr int W = 4, H = 10;
        std::vector<std::uint32_t> buf(W * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        Renderer2D r(fb, 1);
        r.fill_v_gradient(0, 0, W, H, 0xFF000000, 0xFF0000FF);   // black → blue (B: 0→255)
        auto Bc = [&](int y) { return int(buf[y * W] & 0xFF); };
        CHECK(Bc(0) < 30);                                        // top ≈ black
        CHECK(Bc(H - 1) > 225);                                   // bottom ≈ blue
        CHECK(Bc(H / 2) > Bc(0) && Bc(H / 2) < Bc(H - 1));        // increases downward
    }

    // --- blit_scaled: upscale fills blocks, transparent texel skipped ---
    {
        constexpr int W = 8, H = 8;
        std::vector<std::uint32_t> buf(W * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        Renderer2D r(fb, 1);
        // 2x2 source: TL red, TR transparent, BL green, BR blue.
        const std::uint32_t src[4] = {0xFFFF0000, 0x00000000, 0xFF00FF00, 0xFF0000FF};
        gfx::Sprite s{src, 2, 2};
        r.blit_scaled(s, 0, 0, 4, 4);                    // 2x2 -> 4x4 (each texel a 2x2 block)
        CHECK(buf[0] == 0xFFFF0000u && buf[W + 1] == 0xFFFF0000u);   // TL red 2x2 block
        CHECK(buf[2] == BG && buf[3] == BG);                          // TR texel transparent -> bg
        CHECK(buf[2 * W + 0] == 0xFF00FF00u);                         // BL green (row y=2)
        CHECK(buf[2 * W + 2] == 0xFF0000FFu);                         // BR blue
        CHECK(buf[4] == BG);                                          // nothing past the dst rect
        r.blit_scaled(s, 0, 0, 0, 4);                    // non-positive dw -> no-op (no crash)
    }

    if (g_failures == 0) std::printf("aa: all tests passed\n");
    else                 std::printf("aa: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
