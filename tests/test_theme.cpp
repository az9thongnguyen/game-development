// =============================================================================
//  tests/test_theme.cpp  —  sanity checks on the design-system tokens
// =============================================================================
//  Tokens are just constants, but a few relationships MUST hold or the UI stops
//  being legible/consistent: the accent must differ from surfaces, primary text
//  must have real contrast against the background (a WCAG-style ratio floor), and
//  the ordered scales (spacing / radius / type) must be strictly increasing.
// =============================================================================
#include <cmath>
#include <cstdio>

#include "engine/color.hpp"
#include "engine/ui/theme.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

// WCAG relative luminance: sRGB channels linearized, then weighted.
static double lin(double c) {
    c /= 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}
static double luminance(gfx::Color c) {
    return 0.2126 * lin(gfx::r_of(c)) + 0.7152 * lin(gfx::g_of(c)) + 0.0722 * lin(gfx::b_of(c));
}
static double contrast(gfx::Color a, gfx::Color b) {
    const double la = luminance(a), lb = luminance(b);
    const double hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

int main() {
    using namespace ui::theme;

    // Accent is distinct from the surfaces it sits on.
    CHECK(accent != bg && accent != elevated && accent != ctrl);

    // Primary text is readable on the two main surfaces (WCAG AA body ≈ 4.5:1).
    CHECK(contrast(text, bg) >= 4.5);
    CHECK(contrast(text, elevated) >= 4.5);
    // The accent, used for primary buttons, is readable against its on-accent text.
    CHECK(contrast(on_accent, accent) >= 4.5);
    // Muted text is dimmer than primary (a real hierarchy), but still visible.
    CHECK(luminance(text_muted) < luminance(text));
    CHECK(contrast(text_dim, bg) >= 3.0);

    // Ordered scales are strictly increasing.
    CHECK(space_xs < space_sm && space_sm < space_md && space_md < space_lg && space_lg < space_xl);
    CHECK(radius_sm < radius_md);
    CHECK(sz_caption < sz_body && sz_body < sz_label && sz_label < sz_title && sz_title < sz_display);

    // Interaction states brighten monotonically (idle → hover → press).
    CHECK(luminance(ctrl) < luminance(ctrl_hover));
    CHECK(luminance(ctrl_hover) < luminance(ctrl_press));

    if (g_failures == 0) std::printf("theme: all tests passed\n");
    else                 std::printf("theme: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
