// =============================================================================
//  tests/test_aa.cpp  —  anti-aliasing: SSAA coordinate scaling + AA primitives
// =============================================================================
//  No SDL: draw into a plain offscreen framebuffer and inspect pixels.
//  Phase 2.1 covers the SSAA seam (logical→physical scaling); 2.2/2.3 extend this
//  file with Xiaolin Wu lines and coverage-based rounded rects / circles.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <algorithm>
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


    // --- a translucent OUTLINE blends, on its straight edges as well as its arcs ---
    // `draw_round_rect` painted its four edges with an opaque copy and its four corner
    // arcs with the alpha-respecting sink, so one call produced two different things:
    // solid straight edges joined by faint curves. It survived eleven chapters because
    // every outline in the project was opaque until an on-screen d-pad wanted a faint
    // one, and it is invisible in any screenshot you are not staring at.
    {
        constexpr int W = 40, H = 40;
        constexpr std::uint32_t HALF = 0x80FFFFFF;      // white at 50%
        std::vector<std::uint32_t> buf(W * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        Renderer2D r(fb, 1);
        r.draw_round_rect(4, 4, 32, 32, 8, HALF);

        // A pixel in the middle of the top edge, and one in the middle of the left
        // edge: both are straight segments, and both must be BLENDED, not white.
        const std::uint32_t top  = buf[4 * W + 20];
        const std::uint32_t left = buf[20 * W + 4];
        CHECK(top != BG && top != FG);
        CHECK(left != BG && left != FG);
        CHECK(top == left);                              // ...and by the same amount

        // Opaque still lands exactly on the fast path — the fix must not have made
        // every outline pay for a blend it does not need.
        std::fill(buf.begin(), buf.end(), BG);
        r.draw_round_rect(4, 4, 32, 32, 8, FG);
        CHECK(buf[4 * W + 20] == FG);
        CHECK(buf[20 * W + 4] == FG);

        // The same claim for the plain rectangle outline, which shares the sink.
        std::fill(buf.begin(), buf.end(), BG);
        r.draw_rect(4, 4, 32, 32, HALF);
        const std::uint32_t edge = buf[4 * W + 20];
        CHECK(edge != BG && edge != FG);
        CHECK(edge == top);                              // same colour, same maths

        // And the FILL of the same family, so a future "fast path" here has to be a
        // decision rather than a slip.
        std::fill(buf.begin(), buf.end(), BG);
        r.fill_round_rect(4, 4, 32, 32, 8, HALF);
        const std::uint32_t middle = buf[20 * W + 20];
        CHECK(middle != BG && middle != FG);
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

    // ---------------------------------------------------------------------
    //  Clip stack. Without it a panel cannot contain its own contents — the only
    //  bound was the framebuffer edge — so there is no scroll region and no popup
    //  that clips to its frame.
    // ---------------------------------------------------------------------
    {
        constexpr int W = 20, H = 20;
        constexpr std::uint32_t BG = 0xFF000000, FG = 0xFFFFFFFF, FG2 = 0xFF00FF00;
        std::vector<std::uint32_t> buf(static_cast<std::size_t>(W) * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        gfx::Renderer2D r(fb);
        const auto at = [&](int x, int y) { return buf[static_cast<std::size_t>(y) * W + x]; };

        // A fill larger than the clip is cut down to it, on all four sides.
        r.push_clip(5, 5, 6, 6);                 // [5,11) x [5,11)
        r.fill_rect(0, 0, W, H, FG);
        r.pop_clip();
        CHECK(at(4, 8)  == BG);   CHECK(at(5, 8)  == FG);
        CHECK(at(10, 8) == FG);   CHECK(at(11, 8) == BG);
        CHECK(at(8, 4)  == BG);   CHECK(at(8, 5)  == FG);
        CHECK(at(8, 10) == FG);   CHECK(at(8, 11) == BG);

        // After popping, drawing is unrestricted again.
        r.fill_rect(0, 0, 2, 2, FG2);
        CHECK(at(0, 0) == FG2);

        // Nesting INTERSECTS: a child asking for more than its parent still gets
        // only the parent's area. This is the property that makes a scrolled list
        // inside a panel safe.
        for (auto& p : buf) p = BG;
        r.push_clip(5, 5, 6, 6);
        r.push_clip(0, 0, W, H);                 // asks for everything...
        r.fill_rect(0, 0, W, H, FG);             // ...still confined to [5,11)
        r.pop_clip();
        r.pop_clip();
        CHECK(at(4, 8) == BG);
        CHECK(at(5, 8) == FG);
        CHECK(at(11, 8) == BG);

        // An empty intersection draws nothing at all rather than inverting.
        for (auto& p : buf) p = BG;
        r.push_clip(0, 0, 4, 4);
        r.push_clip(10, 10, 4, 4);               // disjoint from the parent
        r.fill_rect(0, 0, W, H, FG);
        r.pop_clip();
        r.pop_clip();
        int lit = 0;
        for (auto p : buf) if (p != BG) ++lit;
        CHECK(lit == 0);

        // The clip binds the anti-aliased sink too, not just solid fills — glyphs,
        // Wu lines and coverage shapes all deposit through blend_cov.
        for (auto& p : buf) p = BG;
        r.push_clip(0, 0, 10, 20);
        r.fill_circle(10, 10, 8, FG);            // centred on the clip's right edge
        r.pop_clip();
        int right_of_clip = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 10; x < W; ++x) if (at(x, y) != BG) ++right_of_clip;
        CHECK(right_of_clip == 0);
        CHECK(at(6, 10) != BG);                  // ...and it did draw inside

        // clear() respects the clip, so a region can be reset without touching
        // the rest of the frame.
        for (auto& p : buf) p = FG;
        r.push_clip(2, 2, 3, 3);
        r.clear(BG);
        r.pop_clip();
        CHECK(at(2, 2) == BG);
        CHECK(at(4, 4) == BG);
        CHECK(at(1, 1) == FG);
        CHECK(at(5, 5) == FG);

        // An unbalanced pop is ignored rather than corrupting the state.
        r.pop_clip(); r.pop_clip();
        for (auto& p : buf) p = BG;
        r.fill_rect(0, 0, 3, 3, FG);
        CHECK(at(0, 0) == FG);
    }

    // Supersampling: the clip is given in LOGICAL coordinates like everything else.
    {
        constexpr int LW = 10, LH = 10, SS = 2;
        constexpr std::uint32_t BG = 0xFF000000, FG = 0xFFFFFFFF;
        std::vector<std::uint32_t> buf(static_cast<std::size_t>(LW * SS) * (LH * SS), BG);
        platform::Framebuffer fb{buf.data(), LW * SS, LH * SS, LW * SS};
        gfx::Renderer2D r(fb, SS);
        r.push_clip(2, 2, 4, 4);
        r.fill_rect(0, 0, LW, LH, FG);
        r.pop_clip();
        const auto phys = [&](int px, int py) { return buf[static_cast<std::size_t>(py) * (LW * SS) + px]; };
        CHECK(phys(3, 5) == BG);       // logical x=1.5 -> outside
        CHECK(phys(4, 5) == FG);       // logical x=2   -> inside
        CHECK(phys(11, 5) == FG);      // logical x=5.5 -> inside
        CHECK(phys(12, 5) == BG);      // logical x=6   -> outside
    }

    if (g_failures == 0) std::printf("aa: all tests passed\n");
    else                 std::printf("aa: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
