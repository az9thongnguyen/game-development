// =============================================================================
//  tests/test_font.cpp  —  unit tests for the stb_truetype font module
// =============================================================================
//  Loads the real Inter face (through the assets seam, resolved against the repo
//  root via the ASSET_ROOT compile definition), then checks parse success/failure,
//  monotonic metrics, that a rasterized glyph actually has coverage, and — the
//  part that used to be broken — that text is walked as UTF-8 code points rather
//  than as bytes.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/assets.hpp"
#include "engine/renderer2d.hpp"
#include "engine/text/font.hpp"
#include "engine/text/utf8.hpp"

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

    // --- Renderer2D text: 8x8 fallback still works; font path is anti-aliased ---
    {
        constexpr int W = 80, H = 28;
        constexpr std::uint32_t BG = 0xFF000000;   // opaque black
        std::vector<std::uint32_t> buf(static_cast<std::size_t>(W) * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};
        gfx::Renderer2D r(fb);

        // No font set → legacy 8x8 path must still draw (regression guard).
        r.draw_text(1, 1, "Hi", 0xFFFFFFFF);
        int lit_fb = 0;
        for (auto p : buf) if (p != BG) ++lit_fb;
        CHECK(lit_fb > 0);

        // Font set → AA glyphs: white-on-black must yield intermediate greys.
        for (auto& p : buf) p = BG;
        r.set_font(font.get(), 18);
        CHECK(r.text_width("Hi") > 0);
        r.draw_text(1, 1, "Hi", 0xFFFFFFFF);
        int lit = 0, grey = 0;
        for (auto p : buf) {
            if (p == BG) continue;
            ++lit;
            const std::uint32_t rr = (p >> 16) & 0xFF;
            if (rr > 0 && rr < 255) ++grey;
        }
        CHECK(lit > 0);
        CHECK(grey > 0);   // anti-aliasing produced partial-coverage pixels
    }

    // ---------------------------------------------------------------------
    //  UTF-8 decoding
    // ---------------------------------------------------------------------
    {
        // Decode a whole string into code points, the way every draw path now does.
        auto decode = [](const char* s) {
            std::vector<char32_t> out;
            const char* end = s;
            while (*end) ++end;
            for (const char* p = s; p < end; ) out.push_back(text::utf8_next(p, end));
            return out;
        };

        CHECK(decode("A")          == std::vector<char32_t>{U'A'});                 // 1 byte
        CHECK(decode("\xC3\xA9")   == std::vector<char32_t>{0x00E9});               // 2 bytes: é
        CHECK(decode("\xE2\x86\x92") == std::vector<char32_t>{0x2192});             // 3 bytes: →
        CHECK(decode("\xE1\xBA\xBF") == std::vector<char32_t>{0x1EBF});             // 3 bytes: ế
        CHECK(decode("\xF0\x9F\x8E\xAE") == std::vector<char32_t>{0x1F3AE});        // 4 bytes: 🎮

        // The regression this whole change exists for: one arrow is ONE code
        // point, not three. Before the decoder it produced three '?' glyphs.
        CHECK(decode("→").size() == 1);
        CHECK(decode("publish→dev").size() == 11);

        // Malformed input yields U+FFFD and consumes exactly one byte, so a bad
        // byte can never swallow the valid text after it.
        CHECK(decode("\x80")             == std::vector<char32_t>{text::kReplacement});   // orphan continuation
        CHECK(decode("\xFF")             == std::vector<char32_t>{text::kReplacement});   // illegal lead
        CHECK((decode("\xE2\x86")     == std::vector<char32_t>{text::kReplacement, text::kReplacement}));  // truncated
        CHECK((decode("\xC0\xAF")     == std::vector<char32_t>{text::kReplacement, text::kReplacement}));  // overlong '/'
        CHECK((decode("\xED\xA0\x80") == std::vector<char32_t>{text::kReplacement, text::kReplacement, text::kReplacement})); // surrogate D800
        CHECK(decode("\xF5\x80\x80\x80") .size() == 4);                                 // > U+10FFFF, all rejected
        CHECK((decode("\xFF" "A")      == std::vector<char32_t>{text::kReplacement, U'A'}));  // one bad byte, then real text

        CHECK(text::utf8_count("abc") == 3);
        CHECK(text::utf8_count("a→c") == 3);      // 5 bytes, 3 characters
        CHECK(text::utf8_count(nullptr) == 0);
    }

    // ---------------------------------------------------------------------
    //  Code-point glyphs: non-ASCII renders as itself, not as '?' repeated
    // ---------------------------------------------------------------------
    {
        // Inter covers both of these; each must be one real, inked glyph.
        for (char32_t cp : {char32_t(0x2192), char32_t(0x1EBF)}) {   // → and ế
            const text::Glyph* g = font->glyph(20, cp);
            CHECK(g != nullptr && g->w > 0 && g->h > 0 && g->cov != nullptr && g->advance > 0);
            if (g && g->cov) {
                int lit = 0;
                for (int i = 0; i < g->w * g->h; ++i) if (g->cov[i] > 0) ++lit;
                CHECK(lit > 0);
            }
        }

        // Measured width must equal ONE arrow, not three question marks.
        CHECK(font->text_width(20, "→") == font->glyph(20, 0x2192)->advance);
        CHECK(font->text_width(20, "→") != 3 * font->glyph(20, U'?')->advance);

        // Width is exactly the sum of the advances the draw loop uses — the two
        // can never disagree because both go through glyph().
        CHECK(font->text_width(20, "a→b") == font->glyph(20, U'a')->advance +
                                             font->glyph(20, 0x2192)->advance +
                                             font->glyph(20, U'b')->advance);

        // A code point the face has no outline for draws a visible hollow box,
        // not nothing and not '?'. Inter is a Latin/Greek/Cyrillic face with no CJK,
        // so U+4E2D is genuinely absent (U+E000 is not a safe choice — fonts often
        // do map the private-use area).
        const text::Glyph* tofu = font->glyph(20, 0x4E2D);
        CHECK(tofu != nullptr && tofu->w > 0 && tofu->h > 0 && tofu->cov != nullptr && tofu->advance > 0);
        if (tofu && tofu->cov && tofu->w > 2 && tofu->h > 2) {
            // Assert the actual shape, not just "some ink": a solid 1px border with
            // a hollow middle. A weaker check would still pass if the face silently
            // mapped this code point to a real outline, and the tofu path would go
            // untested.
            bool border_solid = true, interior_clear = true;
            for (int y = 0; y < tofu->h; ++y)
                for (int x = 0; x < tofu->w; ++x) {
                    const bool edge = (x == 0 || y == 0 || x == tofu->w - 1 || y == tofu->h - 1);
                    const std::uint8_t v = tofu->cov[static_cast<std::size_t>(y) * tofu->w + x];
                    if (edge && v != 255) border_solid  = false;
                    if (!edge && v != 0)  interior_clear = false;
                }
            CHECK(border_solid);
            CHECK(interior_clear);
        }
    }

    // ---------------------------------------------------------------------
    //  Cache: one rasterize per (size, code point), and coverage pointers must
    //  survive the map growing. The cache holds each glyph's coverage in a
    //  std::vector inside the map; rehashing MOVES those, and this is the test
    //  that says the move keeps the heap buffer (a stale `cov` would render
    //  garbage or crash, intermittently, only on large glyph sets).
    // ---------------------------------------------------------------------
    {
        const text::Glyph* a0   = font->glyph(20, U'A');
        const std::uint8_t* cov = a0->cov;
        const int w = a0->w, h = a0->h;
        std::vector<std::uint8_t> before(cov, cov + static_cast<std::size_t>(w) * h);

        // Force many rehashes by pulling in a few hundred fresh code points.
        for (char32_t cp = 0x0400; cp < 0x0600; ++cp) (void)font->glyph(20, cp);

        const text::Glyph* a1 = font->glyph(20, U'A');
        CHECK(a1 == a0);            // same cached Glyph, not a re-rasterized copy
        CHECK(a1->cov == cov);      // and its coverage buffer did not move
        CHECK(std::vector<std::uint8_t>(a1->cov, a1->cov + static_cast<std::size_t>(w) * h) == before);
    }

    // ---------------------------------------------------------------------
    //  Renderer2D: an arrow is one glyph wide on BOTH text paths
    // ---------------------------------------------------------------------
    {
        constexpr int W = 120, H = 40;
        constexpr std::uint32_t BG = 0xFF000000;
        std::vector<std::uint32_t> buf(static_cast<std::size_t>(W) * H, BG);
        platform::Framebuffer fb{buf.data(), W, H, W};

        // 8x8 fallback advances per CHARACTER: "a→b" is 3 glyphs = 24px, not 5 bytes.
        gfx::Renderer2D r8(fb);
        CHECK(r8.text_width("a→b") == 3 * 8);

        // AA path: the arrow measures as one glyph, and drawing it inks pixels.
        gfx::Renderer2D r(fb);
        r.set_font(font.get(), 18);
        CHECK(r.text_width("→") > 0);
        CHECK(r.text_width("a→b") < r.text_width("a???b"));   // one glyph beat three
        r.draw_text(2, 2, "→ ế", 0xFFFFFFFF);
        int lit = 0;
        for (auto p : buf) if (p != BG) ++lit;
        CHECK(lit > 0);
    }

    // ---------------------------------------------------------------------
    //  text_width rounds rather than truncates across supersample factors
    // ---------------------------------------------------------------------
    {
        constexpr int LW = 100, LH = 30;
        for (int ss : {1, 2, 3}) {
            std::vector<std::uint32_t> buf(static_cast<std::size_t>(LW * ss) * (LH * ss), 0xFF000000);
            platform::Framebuffer fb{buf.data(), LW * ss, LH * ss, LW * ss};
            gfx::Renderer2D r(fb, ss);
            r.set_font(font.get(), 14);
            const int phys = font->text_width(14 * ss, "Publish");
            CHECK(r.text_width("Publish") == (phys + ss / 2) / ss);   // nearest, not floor
        }
    }

    if (g_failures == 0) std::printf("font: all tests passed\n");
    else                 std::printf("font: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
