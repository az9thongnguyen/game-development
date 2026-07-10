// =============================================================================
//  tests/test_font.cpp  —  unit tests for the stb_truetype font module
// =============================================================================
//  Loads the real Inter face (through the assets seam, resolved against the repo
//  root via the ASSET_ROOT compile definition), then checks parse success/failure,
//  monotonic metrics, and that a rasterized glyph actually has coverage.
// =============================================================================
#include <cstdio>

#include "engine/assets.hpp"
#include "engine/text/font.hpp"

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
    assets::set_base_path(ASSET_ROOT);

    // --- bad data fails cleanly ---
    CHECK(text::Font::load_from_bytes({}) == nullptr);
    CHECK(text::Font::load_from_bytes({0x00, 0x01, 0x02, 0x03}) == nullptr);

    // --- load the real face ---
    auto bytes = assets::load_file("assets/fonts/Inter.ttf");
    CHECK(bytes.has_value());
    if (!bytes) { std::printf("font: cannot open Inter.ttf under %s\n", ASSET_ROOT); return 1; }

    auto font = text::Font::load_from_bytes(std::move(*bytes));
    CHECK(font != nullptr);
    if (!font) return 1;

    // --- metrics are sane and monotonic ---
    CHECK(font->text_width(16, "A") > 0);
    CHECK(font->text_width(16, "AB") > font->text_width(16, "A"));   // longer is wider
    CHECK(font->text_width(24, "AB") > font->text_width(12, "AB"));  // bigger is wider
    CHECK(font->line_height(16) > 0);
    CHECK(font->ascent(16) > 0 && font->ascent(16) < font->line_height(16));

    // --- a rasterized glyph has real coverage ---
    const text::Glyph* A = font->glyph(24, 'A');
    CHECK(A != nullptr);
    CHECK(A->w > 0 && A->h > 0 && A->cov != nullptr);
    if (A && A->cov) {
        int lit = 0;
        for (int i = 0; i < A->w * A->h; ++i) if (A->cov[i] > 0) ++lit;
        CHECK(lit > 0);                          // 'A' is not blank
        CHECK(lit < A->w * A->h);                // ...nor fully solid (it has AA edges/holes)
    }

    // --- space is a valid, blank, positive-advance glyph ---
    const text::Glyph* sp = font->glyph(24, ' ');
    CHECK(sp != nullptr && sp->cov == nullptr && sp->advance > 0);

    if (g_failures == 0) std::printf("font: all tests passed\n");
    else                 std::printf("font: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
