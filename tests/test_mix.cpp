// =============================================================================
//  tests/test_mix.cpp  —  the fourth door into .hrt, driven with no files at all
// =============================================================================
//  `mix::compose` takes its part images from a resolver, so every case here is built
//  out of pixels this file made up. That is the point of the seam: a composition
//  test that needed art on a disk would be testing the disk.
// =============================================================================
#include "engine/mix/mix.hpp"

#include <cstdio>
#include <map>
#include <string>

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

// A 4x2 sheet of 2px tiles: tile 0 solid red, 1 solid green, 2 half-transparent blue
// over its bottom row, 3..7 empty. Small enough to reason about by hand.
gfx::Image sheet() {
    gfx::Image img;
    img.w = 8; img.h = 4;
    img.pixels.assign(32, 0);
    const auto put = [&](int x, int y, gfx::Color c) {
        img.pixels[static_cast<std::size_t>(y) * 8 + x] = c;
    };
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            put(x,     y, gfx::rgb(0xE0, 0x40, 0x40));       // tile 0
            put(2 + x, y, gfx::rgb(0x40, 0xE0, 0x40));       // tile 1
        }
    put(4, 1, gfx::rgba(0x40, 0x40, 0xE0, 128));             // tile 2, one soft pixel
    return img;
}

std::function<const gfx::Image*(const std::string&)> resolver(const gfx::Image& img) {
    return [&img](const std::string& n) -> const gfx::Image* {
        return n == "s" ? &img : nullptr;
    };
}

const char* const kGood =
    "mix1\n"
    "name t\n"
    "size 4 4\n"
    "sheet s parts.hrt 2\n"
    "part s 0 at 0 0\n"
    "part s 1 at 2 2\n"
    "swap e04040 101010\n";

} // namespace

static void test_parse_and_roundtrip() {
    std::string why;
    auto m = mix::parse_mix(kGood, &why);
    CHECK(m.has_value());
    if (!m) { std::printf("      why: %s\n", why.c_str()); return; }
    CHECK(m->name == "t");
    CHECK(m->w == 4 && m->h == 4);
    CHECK(m->sheets.size() == 1 && m->sheets[0].tile == 2);
    CHECK(m->parts.size() == 2);
    CHECK(m->swaps.size() == 1);
    CHECK(mix::to_text(*m) == kGood);          // what it wrote is what it reads
    CHECK(mix::to_text(*mix::parse_mix(mix::to_text(*m))) == kGood);
}

static void test_refusals() {
    const auto bad = [](const std::string& text) {
        std::string why;
        const bool refused = !mix::parse_mix(text, &why).has_value();
        // A refusal with no reason is barely better than a silent default: the whole
        // argument for a strict format is that it says which line and what.
        if (refused && why.empty()) return false;
        return refused;
    };
    CHECK(bad(""));                                                  // not a mix file
    CHECK(bad("size 4 4\nsheet s p.hrt 2\npart s 0 at 0 0\n"));      // no magic first
    // ...and a junk first record must be REFUSED, not swallowed as the magic. This
    // file is otherwise complete, so "whatever comes first is the header" would
    // accept it — and then any text file with the right records in it is a mix.
    CHECK(bad("banana\nname x\nsize 4 4\nsheet s p.hrt 2\npart s 0 at 0 0\n"));
    CHECK(bad("mix1\nsize 4 4\n"));                                  // no parts
    CHECK(bad("mix1\nsheet s p.hrt 2\npart s 0 at 0 0\n"));          // no size
    CHECK(bad("mix1\nsize 4 4\nsize 5 5\nsheet s p.hrt 2\npart s 0 at 0 0\n"));
    CHECK(bad("mix1\nsize 0 4\nsheet s p.hrt 2\npart s 0 at 0 0\n"));
    CHECK(bad("mix1\nsize 99999 4\nsheet s p.hrt 2\npart s 0 at 0 0\n"));
    CHECK(bad("mix1\nsize 4 4\nsheet s p.hrt 2\nsheet s q.hrt 2\npart s 0 at 0 0\n"));
    // The one that must never be silent: a typo in a sheet name would otherwise read
    // as "that part has no art yet" and compose a sprite with a hole in it.
    CHECK(bad("mix1\nsize 4 4\nsheet s p.hrt 2\npart body 0 at 0 0\n"));
    CHECK(bad("mix1\nsize 4 4\nsheet s p.hrt 2\npart s -1 at 0 0\n"));
    CHECK(bad("mix1\nsize 4 4\nsheet s p.hrt 2\npart s 0 0 0\n"));          // no `at`
    CHECK(bad("mix1\nsize 4 4\nsheet s p.hrt 2\npart s 0 at 0 0\nswap zz 000000\n"));
    // Seven hex digits is neither rrggbb nor rrggbbaa. "At least six" would take it
    // and read the wrong four bytes out of it — a colour that is nearly right.
    CHECK(bad("mix1\nsize 4 4\nsheet s p.hrt 2\npart s 0 at 0 0\nswap 1234567 000000\n"));
    CHECK(bad("mix1\nsize 4 4\nsheet s p.hrt 2\npart s 0 at 0 0\nswap 12345 000000\n"));
    CHECK(bad("mix1\nsize 4 4\nsheet s p.hrt 2\npart s 0 at 0 0\nswap 010203 010203\n"));
    CHECK(bad("mix1\nsize 4 4\nsheet s p.hrt 2\npart s 0 at 0 0\nwiggle 3\n"));  // unknown

    // ...and comments and blank lines before the magic are fine, because a file that
    // cannot carry a header comment is a file nobody explains.
    CHECK(mix::parse_mix("# hello\n\nmix1\nsize 2 2\nsheet s p.hrt 2\npart s 0 at 0 0\n")
              .has_value());
}

static void test_compose() {
    const gfx::Image img = sheet();
    auto m = mix::parse_mix(kGood);
    CHECK(m.has_value());
    if (!m) return;

    std::string why;
    auto out = mix::compose(*m, resolver(img), &why);
    CHECK(out.has_value());
    if (!out) { std::printf("      why: %s\n", why.c_str()); return; }
    CHECK(out->w == 4 && out->h == 4);

    const auto px = [&](int x, int y) { return out->pixels[static_cast<std::size_t>(y) * 4 + x]; };
    // Tile 0 was red and the swap turned it dark; tile 1 stayed green.
    CHECK(px(0, 0) == gfx::rgb(0x10, 0x10, 0x10));
    CHECK(px(1, 1) == gfx::rgb(0x10, 0x10, 0x10));
    CHECK(px(2, 2) == gfx::rgb(0x40, 0xE0, 0x40));
    CHECK(px(3, 0) == 0);                       // nothing was drawn there

    // Declaration order is stacking order: the second part wins where they overlap.
    auto over = mix::parse_mix("mix1\nsize 2 2\nsheet s p.hrt 2\n"
                               "part s 0 at 0 0\npart s 1 at 0 0\n");
    CHECK(over.has_value());
    auto o2 = over ? mix::compose(*over, resolver(img)) : std::nullopt;
    CHECK(o2.has_value());
    if (o2) CHECK(o2->pixels[0] == gfx::rgb(0x40, 0xE0, 0x40));

    // A partly-transparent pixel BLENDS with what is under it rather than replacing
    // it — which is the difference between a mixer and a stack of stickers.
    auto blend = mix::parse_mix("mix1\nsize 2 2\nsheet s p.hrt 2\n"
                                "part s 0 at 0 0\npart s 2 at 0 0\n");
    CHECK(blend.has_value());
    auto b2 = blend ? mix::compose(*blend, resolver(img)) : std::nullopt;
    CHECK(b2.has_value());
    if (b2) {
        const gfx::Color c = b2->pixels[2];      // the soft blue pixel, over red
        CHECK(gfx::a_of(c) == 255);
        CHECK(gfx::b_of(c) > 0x60 && gfx::b_of(c) < 0xE0);   // neither pure red nor blue
        CHECK(gfx::r_of(c) > 0x40);
        CHECK(b2->pixels[0] == gfx::rgb(0xE0, 0x40, 0x40));  // ...and only that pixel
    }
}

static void test_compose_refusals() {
    const gfx::Image img = sheet();
    const auto why_of = [&](const char* text) {
        auto m = mix::parse_mix(text);
        if (!m) return std::string("PARSE");
        std::string why;
        return mix::compose(*m, resolver(img), &why).has_value() ? std::string() : why;
    };
    // A sheet the caller cannot supply.
    CHECK(!why_of("mix1\nsize 2 2\nsheet nope p.hrt 2\npart nope 0 at 0 0\n").empty());
    // A tile past the end of the sheet: 8x4 at 2px is 8 tiles, so 8 is one too far.
    CHECK(why_of("mix1\nsize 2 2\nsheet s p.hrt 2\npart s 7 at 0 0\n").empty());
    CHECK(!why_of("mix1\nsize 2 2\nsheet s p.hrt 2\npart s 8 at 0 0\n").empty());
    // A tile size the sheet cannot hold one of.
    CHECK(!why_of("mix1\nsize 2 2\nsheet s p.hrt 16\npart s 0 at 0 0\n").empty());
    // Entirely off the canvas is a typo...
    CHECK(!why_of("mix1\nsize 2 2\nsheet s p.hrt 2\npart s 0 at 2 0\n").empty());
    CHECK(!why_of("mix1\nsize 2 2\nsheet s p.hrt 2\npart s 0 at -2 0\n").empty());
    // ...but PARTLY off is a crop, and that is how a hat hangs over the top edge.
    CHECK(why_of("mix1\nsize 2 2\nsheet s p.hrt 2\npart s 0 at -1 0\n").empty());
    CHECK(why_of("mix1\nsize 2 2\nsheet s p.hrt 2\npart s 0 at 1 1\n").empty());
}

// A swap changes a COLOUR, not a silhouette. Every other case here composites onto
// something opaque, so alpha 255 comes out either way and the distinction is invisible
// — which is exactly when it goes wrong unnoticed.
static void test_swap_keeps_alpha() {
    const gfx::Image img = sheet();
    // Tile 2 has one half-transparent blue pixel and nothing under it.
    auto m = mix::parse_mix("mix1\nsize 2 2\nsheet s p.hrt 2\npart s 2 at 0 0\n"
                            "swap 4040e0 20c020\n");
    CHECK(m.has_value());
    if (!m) return;
    auto out = mix::compose(*m, resolver(img));
    CHECK(out.has_value());
    if (!out) return;
    const gfx::Color c = out->pixels[2];
    CHECK(gfx::r_of(c) == 0x20 && gfx::g_of(c) == 0xc0 && gfx::b_of(c) == 0x20);
    CHECK(gfx::a_of(c) == 128);      // the pixel is still as see-through as it was
}

static void test_deterministic() {
    // The whole positioning of this format against an AI generator: the same source
    // makes the same pixels, every time, on every machine.
    const gfx::Image img = sheet();
    auto m = mix::parse_mix(kGood);
    CHECK(m.has_value());
    if (!m) return;
    const auto a = mix::compose(*m, resolver(img));
    const auto b = mix::compose(*m, resolver(img));
    CHECK(a.has_value() && b.has_value());
    if (a && b) CHECK(a->pixels == b->pixels);
}

int main() {
    test_parse_and_roundtrip();
    test_refusals();
    test_compose();
    test_compose_refusals();
    test_swap_keeps_alpha();
    test_deterministic();
    if (g_failures == 0) std::printf("mix: all tests passed\n");
    else                 std::printf("mix: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
