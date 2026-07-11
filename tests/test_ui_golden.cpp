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

    if (g_failures == 0) std::printf("ui_golden: all tests passed\n");
    else                 std::printf("ui_golden: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
