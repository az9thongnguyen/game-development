// =============================================================================
//  tests/test_ui_golden.cpp  —  render a representative UI and check it structurally
// =============================================================================
//  Draws a panel + normal/primary buttons + checkbox + slider into an OFFSCREEN
//  framebuffer at ss=2 (so SSAA + AA + font all run), dumps a PPM for human eyeball
//  review, and asserts robust invariants: solid fills hit their exact token colour
//  (portable — only edges are AA), and anti-aliased pixels exist.
//
//  NB: we deliberately do NOT assert a pixel-exact checksum — analytic AA rounds
//  differently across compilers/arches (native vs web), so a hash isn't portable.
//  Solid-region colour checks + an AA-present check catch real regressions and stay
//  stable everywhere.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/assets.hpp"
#include "engine/renderer2d.hpp"
#include "engine/text/font.hpp"
#include "engine/ui/theme.hpp"
#include "engine/ui/ui.hpp"

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

int main() {
    namespace th = ui::theme;
    assets::set_base_path(ASSET_ROOT);

    // Optional font (the check still runs without it — text just falls back to 8x8).
    std::unique_ptr<text::Font> font;
    if (auto b = assets::load_file("assets/fonts/Inter.ttf")) font = text::Font::load_from_bytes(std::move(*b));

    constexpr int LW = 200, LH = 160, SS = 2;
    constexpr int PW = LW * SS, PH = LH * SS;
    std::vector<std::uint32_t> buf(static_cast<std::size_t>(PW) * PH, th::bg);
    platform::Framebuffer fb{buf.data(), PW, PH, PW};
    gfx::Renderer2D r(fb, SS);
    if (font) r.set_font(font.get(), th::sz_body);

    // Draw the scene with EXPLICIT rects so we know where solid regions are.
    ui::Context ui;
    ui.begin(&r, ui::Input{-1, -1, false, false, false});     // no hover/interaction
    ui.panel(ui::Rect{4, 4, 192, 152}, nullptr);
    ui.button(ui::Rect{16, 20, 120, 28}, "Normal");
    ui.button(ui::Rect{16, 56, 120, 28}, "Primary", /*primary*/true);
    bool checked = true;
    ui.checkbox(ui::Rect{16, 92, 20, 20}, "Enabled", checked);
    float v = 0.6f;
    ui.slider(ui::Rect{16, 128, 120, 10}, "Speed", v, 0.0f, 1.0f);
    ui.end();

    // Physical index of a LOGICAL point (sample the top-left of its ss×ss block).
    auto at = [&](int lx, int ly) { return buf[(ly * SS) * PW + (lx * SS)]; };

    // Solid-fill invariants (exact — interiors are not anti-aliased):
    CHECK(at(170, 140) == th::elevated);   // bare panel interior (no widget there)
    CHECK(at(20, 34)   == th::ctrl);       // normal button body (left of centred label)
    CHECK(at(20, 70)   == th::accent);     // primary button body (accent fill)

    // Anti-aliasing actually happened somewhere (rounded corners / glyphs).
    int aa = 0;
    for (auto p : buf) {
        const std::uint32_t px = p;
        if (px != th::bg && px != th::elevated && px != th::ctrl && px != th::accent &&
            px != th::track && px != th::border)
            ++aa;
    }
    CHECK(aa > 0);

    // Dump a PPM next to the test binary for eyeball review.
    if (FILE* f = std::fopen("ui_golden.ppm", "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", PW, PH);
        for (auto p : buf) {
            const unsigned char rgb[3] = {
                static_cast<unsigned char>((p >> 16) & 0xFF),
                static_cast<unsigned char>((p >> 8) & 0xFF),
                static_cast<unsigned char>(p & 0xFF)};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
    }

    // Semantic components share one visual vocabulary: a selected card, destructive
    // action, quiet action, progress meter and a small vector icon. These are exact
    // token checks away from text/AA edges, so a palette refactor remains intentional.
    {
        constexpr int CW = 320, CH = 128;
        std::vector<std::uint32_t> cb(static_cast<std::size_t>(CW * SS) * CH * SS, th::bg);
        platform::Framebuffer cfb{cb.data(), CW * SS, CH * SS, CW * SS};
        gfx::Renderer2D cr(cfb, SS);
        if (font) cr.set_font(font.get(), th::sz_body);
        ui::Context components;
        components.begin(&cr, ui::Input{-1, -1, false, false, false}, CW, CH);
        components.card(ui::Rect{4, 4, 312, 120}, true);
        components.button(ui::Rect{16, 16, 132, 34}, "Delete",
                          ui::ButtonOptions{ui::ButtonKind::Danger, true,
                                            ui::Icon::Close, "Del"});
        components.button(ui::Rect{156, 16, 132, 34}, "Preview",
                          ui::ButtonOptions{ui::ButtonKind::Ghost, true,
                                            ui::Icon::Play, nullptr});
        components.meter(ui::Rect{16, 76, 272, 8}, 0.5f, ui::Tone::Success);
        components.meter(ui::Rect{16, 92, 100, 8}, -0.2f, ui::Tone::Danger);
        components.meter(ui::Rect{128, 92, 100, 8}, 1.2f, ui::Tone::Success);
        components.draw_icon(ui::Rect{270, 94, 18, 18}, ui::Icon::Check, th::success);
        components.end();

        const auto cat = [&](int x, int y) {
            return cb[static_cast<std::size_t>(y * SS) * CW * SS + x * SS];
        };
        CHECK(cat(70, 32) == th::danger);          // destructive button body
        CHECK(cat(165, 32) == th::elevated);       // ghost keeps the card surface
        CHECK(cat(20, 80) == th::success);         // filled half
        CHECK(cat(220, 80) == th::track);          // unfilled half
        CHECK(cat(60, 96) == th::track);            // below zero clamps empty
        CHECK(cat(170, 96) == th::success);         // above one clamps full
        CHECK(cat(240, 96) == th::surface_selected); // ...without spilling past its rect
        CHECK(cat(300, 100) == th::surface_selected);

        int icon_ink = 0;
        for (int y = 94 * SS; y < 112 * SS; ++y)
            for (int x = 270 * SS; x < 288 * SS; ++x)
                if (cb[static_cast<std::size_t>(y) * CW * SS + x] == th::success) ++icon_ink;
        CHECK(icon_ink > 0);

        if (FILE* f = std::fopen("ui_components.ppm", "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", CW * SS, CH * SS);
            for (auto p : cb) {
                const unsigned char rgb[3] = {
                    static_cast<unsigned char>((p >> 16) & 0xFF),
                    static_cast<unsigned char>((p >> 8) & 0xFF),
                    static_cast<unsigned char>(p & 0xFF)};
                std::fwrite(rgb, 1, 3, f);
            }
            std::fclose(f);
        }
    }

    // ---- the status strip says two things at once (chapter 144) -----------------
    // Drawn into its OWN buffer so the count is of this widget and nothing else. What
    // is under test is a claim about COLOUR, and colour is invisible to every other
    // metric this file uses: the same cells in one tone draw the same number of
    // pixels, so an ink count, a checksum and a frame diff all pass a strip that has
    // forgotten how to warn.
    {
        constexpr int SW = 320 * SS, SH = 24 * SS;
        const auto count = [](const std::vector<std::uint32_t>& b, std::uint32_t c) {
            int n = 0;
            for (auto p : b) if (p == c) ++n;
            return n;
        };
        const auto strip = [&](const std::vector<ui::Seg>& segs) {
            std::vector<std::uint32_t> b(static_cast<std::size_t>(SW) * SH, th::bg);
            platform::Framebuffer sfb{b.data(), SW, SH, SW};
            gfx::Renderer2D sr(sfb, SS);
            if (font) sr.set_font(font.get(), th::sz_body);
            ui::Context sui;
            sui.begin(&sr, ui::Input{-1, -1, false, false, false}, 320, 24);
            sui.status_bar(ui::Rect{0, 0, 320, 24}, segs, "Cmd+S save");
            sui.end();
            return b;
        };

        const auto dirty = strip({{"textures/hero.hrt"},
                                  {"unsaved", ui::Tone::Warning},
                                  {"12, 7"},
                                  {"#3aa0ff"}});
        const auto clean = strip({{"textures/hero.hrt"},
                                  {"saved", ui::Tone::Success},
                                  {"12, 7"},
                                  {"#3aa0ff"}});

        // The dirty strip warns...
        CHECK(count(dirty, th::warn) > 0);
        // ...and the clean one does not. Without this direction the check passes on a
        // strip that paints every cell warn, which is the bug being fixed.
        CHECK(count(clean, th::warn) == 0);
        CHECK(count(clean, th::success) > 0);
        CHECK(count(dirty, th::success) == 0);
        // And the cells that are NOT the warning are still drawn in the ordinary
        // colour beside it — the whole reason the strip stopped being one string.
        CHECK(count(dirty, th::text_dim) > 0);
        // The right-hand hint fits in 320px and is drawn in its own quiet colour.
        CHECK(count(dirty, th::text_muted) > 0);

        // An EMPTY cell is not a cell: no text, and no separator either. Asserted as
        // an exact pixel identity rather than by counting dots, because the separator
        // is drawn in the same colour as the hint beside it — the two strips below
        // must be the SAME image, which is a claim no count can weaken.
        {
            const auto three = strip({{"AAA"}, {""}, {"BBB"}});
            const auto two   = strip({{"AAA"}, {"BBB"}});
            CHECK(three == two);
            // ...and the strip they are both compared against is not blank.
            CHECK(three != strip({{"AAA"}, {"CCC"}}));
        }

        // ...and a picture of it, because "the warning is the one cell that warns" is a
        // claim about what a person sees, and a count of pixels is only its shadow.
        if (FILE* f = std::fopen("ui_status.ppm", "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", SW, SH);
            for (auto p : dirty) {
                const unsigned char rgb[3] = {
                    static_cast<unsigned char>((p >> 16) & 0xFF),
                    static_cast<unsigned char>((p >> 8) & 0xFF),
                    static_cast<unsigned char>(p & 0xFF)};
                std::fwrite(rgb, 1, 3, f);
            }
            std::fclose(f);
        }
    }

    if (g_failures == 0) std::printf("ui_golden: all tests passed\n");
    else                 std::printf("ui_golden: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
