// =============================================================================
//  tests/test_paint.cpp  —  pixel edits, and the undo that has to be exact
// =============================================================================
//  The load-bearing claim of this file is not "painting changes pixels". It is that
//  every edit is REVERSIBLE to the exact bytes that were there before — across a
//  stroke that crossed several colours, a fill that touched thousands of pixels, and
//  a redo that has to be idempotent because the image is already at the after-state
//  when the command is first pushed.
//
//  An editor whose undo is approximately right is worse than one with no undo: the
//  first teaches you to trust it.
// =============================================================================
#include <cstdio>
#include <string>
#include <vector>

#include "engine/document/command_stack.hpp"
#include "engine/image.hpp"
#include "engine/paint/paint.hpp"
#include "engine/paint/pixel_source.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

constexpr gfx::Color kA = 0xFF102030u;
constexpr gfx::Color kB = 0xFF405060u;
constexpr gfx::Color kC = 0xFF708090u;
constexpr gfx::Color kClear = 0x00000000u;

gfx::Image make(int w, int h, gfx::Color fill) {
    gfx::Image img;
    img.w = w;
    img.h = h;
    img.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), fill);
    return img;
}

gfx::Color at(const gfx::Image& img, int x, int y) {
    return img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.w) +
                      static_cast<std::size_t>(x)];
}

void put(gfx::Image& img, int x, int y, gfx::Color c) {
    img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.w) +
               static_cast<std::size_t>(x)] = c;
}

// ---------------------------------------------------------------------------
//  Rect: clamped to the image, corners in any order.
// ---------------------------------------------------------------------------
void test_rect() {
    gfx::Image img = make(8, 8, kA);

    const auto px = paint::rect_pixels(img, 1, 1, 2, 3, kB);
    CHECK(px.size() == 2 * 3);
    for (const auto& p : px) CHECK(p.before == kA && p.after == kB);

    // Corners in the other order describe the same rectangle. An editor where
    // dragging up-left selects nothing is an editor with a bug nobody reports,
    // because everyone learns to drag the other way.
    CHECK(paint::rect_pixels(img, 2, 3, 1, 1, kB).size() == 6);

    // Clamped, not refused: a drag that leaves the canvas is a normal gesture.
    const auto over = paint::rect_pixels(img, -5, -5, 100, 100, kB);
    CHECK(over.size() == 8 * 8);
    const auto outside = paint::rect_pixels(img, 20, 20, 30, 30, kB);
    CHECK(outside.empty());

    // A zero-size image cannot be painted, and asking must not read pixel [0].
    gfx::Image empty = make(0, 0, kA);
    CHECK(paint::rect_pixels(empty, 0, 0, 4, 4, kB).empty());
}

// ---------------------------------------------------------------------------
//  Fill: 4-connected, and it matches on the WHOLE colour including alpha.
// ---------------------------------------------------------------------------
void test_fill() {
    gfx::Image img = make(5, 5, kA);
    // A wall down the middle: the fill must stop at it.
    for (int y = 0; y < 5; ++y) put(img, 2, y, kB);

    const auto left = paint::flood_pixels(img, 0, 0, kC);
    CHECK(left.size() == 2 * 5);                  // columns 0 and 1 only
    for (const auto& p : left) CHECK(p.x < 2);

    // Diagonally connected is NOT connected. Four-connected is the rule every pixel
    // editor uses, and eight-connected would leak through a one-pixel diagonal seam.
    gfx::Image diag = make(3, 3, kB);
    put(diag, 0, 0, kA);
    put(diag, 1, 1, kA);
    CHECK(paint::flood_pixels(diag, 0, 0, kC).size() == 1);

    // Filling with the colour that is already there is nothing, not everything.
    CHECK(paint::flood_pixels(img, 0, 0, kA).empty());

    // Alpha is part of the colour. Two pixels that differ only in alpha are different
    // pixels; a fill that compared RGB would quietly make a transparent area opaque.
    gfx::Image alpha = make(4, 1, kClear);
    put(alpha, 0, 0, 0xFF000000u);                // same RGB, opaque
    const auto trans = paint::flood_pixels(alpha, 1, 0, kC);
    CHECK(trans.size() == 3);

    // Off the canvas is empty, not a crash.
    CHECK(paint::flood_pixels(img, -1, 0, kC).empty());
    CHECK(paint::flood_pixels(img, 0, 99, kC).empty());
}

// ---------------------------------------------------------------------------
//  Commands: exact undo, and no empty steps.
// ---------------------------------------------------------------------------
void test_command() {
    gfx::Image img = make(4, 4, kA);
    put(img, 0, 0, kB);          // one pixel differs, so undo has two values to restore
    const gfx::Image before = img;

    doc::CommandStack stack;
    auto cmd = paint::make_command(img, paint::rect_pixels(img, 0, 0, 1, 1, kC), "rect");
    CHECK(cmd.has_value());
    if (!cmd) return;
    stack.push_apply(*cmd);

    CHECK(at(img, 0, 0) == kC && at(img, 1, 1) == kC);
    CHECK(at(img, 2, 2) == kA);
    CHECK(stack.dirty());

    CHECK(stack.undo());
    // Byte for byte, INCLUDING the pixel that was a different colour to begin with.
    // A stroke that restores one uniform "previous colour" passes a weaker test and
    // loses work the moment somebody paints over two colours at once.
    CHECK(img.pixels == before.pixels);
    CHECK(!stack.dirty());

    CHECK(stack.redo());
    CHECK(at(img, 0, 0) == kC && at(img, 1, 1) == kC);

    // An edit that changes nothing is not an edit. Without this, Ctrl+Z would consume
    // a press and appear not to work.
    CHECK(!paint::make_command(img, paint::rect_pixels(img, 0, 0, 1, 1, kC), "again"));
    std::vector<paint::PixelEdit> mixed = paint::rect_pixels(img, 0, 0, 3, 3, kC);
    paint::drop_noops(mixed);
    CHECK(mixed.size() == 4 * 4 - 4);   // the four already painted are gone
}

// ---------------------------------------------------------------------------
//  Stroke: writes through as it goes, reaches the stack ONCE.
// ---------------------------------------------------------------------------
void test_stroke() {
    gfx::Image img = make(8, 8, kA);
    put(img, 3, 0, kB);          // the stroke will cross a second colour
    const gfx::Image before = img;

    doc::CommandStack stack;
    paint::Stroke s;
    s.begin(img, kC, "pencil");
    CHECK(s.active());
    for (int x = 0; x < 5; ++x) s.touch(x, 0);
    // You have to SEE the paint under the cursor: the image changes during the drag,
    // not when the button comes up.
    CHECK(at(img, 0, 0) == kC);
    CHECK(s.touched() == 5);

    // Dragging back over the stroke's own pixels adds nothing — they already hold the
    // colour, which is why no visited set is needed.
    s.touch(0, 0);
    s.touch(1, 0);
    CHECK(s.touched() == 5);
    // ...and neither does leaving the canvas.
    s.touch(-1, 0);
    s.touch(0, 99);
    CHECK(s.touched() == 5);

    auto cmd = s.finish();
    CHECK(!s.active());
    CHECK(cmd.has_value());
    if (!cmd) return;
    stack.push_apply(*cmd);
    CHECK(stack.undo_depth() == 1);          // ONE step for the whole gesture

    CHECK(stack.undo());
    CHECK(img.pixels == before.pixels);      // including the pixel that was kB
    CHECK(stack.redo());
    CHECK(at(img, 3, 0) == kC);
    // Redo runs an apply that was already a no-op the first time: it writes absolute
    // values, not a delta, so running it twice lands in the same place.
    cmd->apply();
    CHECK(at(img, 3, 0) == kC);

    // A gesture that painted nothing is not a step.
    paint::Stroke none;
    none.begin(img, kC, "pencil");
    none.touch(0, 0);                        // already kC
    CHECK(!none.finish());
}

// ---------------------------------------------------------------------------
//  Interpolation: the pointer moves faster than one pixel per frame.
// ---------------------------------------------------------------------------
void test_stroke_line() {
    gfx::Image img = make(16, 16, kA);
    paint::Stroke s;
    s.begin(img, kC, "pencil");
    // Two frames of an ordinary gesture at 8x zoom are several image pixels apart.
    // Touching only the endpoints draws a dotted line, which is the single most
    // obvious way a pixel editor can feel broken.
    s.touch_line(0, 0, 9, 3);
    CHECK(s.touched() == 10);            // one per column, none missing
    for (int x = 0; x <= 9; ++x) {
        bool any = false;
        for (int y = 0; y < 16; ++y) if (at(img, x, y) == kC) any = true;
        CHECK(any);                      // no gap in the line
    }

    // Both diagonal directions, and a single point.
    gfx::Image up = make(8, 8, kA);
    paint::Stroke t;
    t.begin(up, kC, "pencil");
    t.touch_line(7, 7, 0, 0);
    CHECK(t.touched() == 8);
    t.touch_line(3, 3, 3, 3);            // already painted: adds nothing
    CHECK(t.touched() == 8);
}

// -----------------------------------------------------------------------------
//  The ASCII pixel source. What is under test is not "it makes an image" — it is
//  that every way of getting it WRONG is refused. A pixel format that eats a typo
//  produces a hole nobody looks at twice, which is exactly the bug this format was
//  introduced to avoid in a sixteen-piece autotile set.
// -----------------------------------------------------------------------------
static const char* kGood =
    "# a two-tile strip\n"
    "size 2\n"
    "grid 2 1\n"
    "palette . 00000000\n"
    "palette r ff0000\n"
    "palette b 0000ff80\n"
    "\n"
    "tile 0\n"
    "r.\n"
    ".r\n"
    "tile 1\n"
    "bb\n"
    "bb\n";

static void test_pixels_good() {
    std::string why = "untouched";
    const auto img = paint::bake_pixels(kGood, &why);
    CHECK(img.has_value());
    if (!img) return;
    CHECK(img->w == 4 && img->h == 2);
    // Tile 0 goes left, tile 1 right — row-major over the grid.
    CHECK(img->pixels[0] == gfx::rgba(255, 0, 0, 255));
    CHECK(img->pixels[1] == 0u);                       // '.' is transparent, alpha 0
    CHECK(img->pixels[2] == gfx::rgba(0, 0, 255, 128));
    // Six hex digits means opaque; eight carries the alpha through untouched.
    CHECK(gfx::a_of(img->pixels[0]) == 255);
    CHECK(gfx::a_of(img->pixels[2]) == 128);
    // The second row of tile 0 is offset by the SHEET width, not the tile width —
    // the bug every sprite-sheet writer makes once.
    CHECK(img->pixels[4] == 0u);
    CHECK(img->pixels[5] == gfx::rgba(255, 0, 0, 255));
    // A colour is BARE hex. '#' opens a comment, so `#10203040` is not a dark
    // colour, it is an empty palette line — and that has to be refused loudly rather
    // than defaulted, because the habit of writing '#rrggbb' is universal.
    std::string hash_why;
    CHECK(!paint::bake_pixels("size 1\ngrid 1 1\npalette x #10203040\ntile 0\nx\n", &hash_why));
    const auto bare = paint::bake_pixels("size 1\ngrid 1 1\npalette x 10203040\ntile 0\nx\n");
    CHECK(bare && bare->pixels[0] == gfx::rgba(0x10, 0x20, 0x30, 0x40));
}

// Every refusal, each with the line it happened on — an error that does not name the
// line is a search, not a message.
static void test_pixels_refusals() {
    struct Case { const char* text; const char* what; };
    const Case cases[] = {
        {"size 2\ngrid 1 1\npalette . 00000000\ntile 0\n..\n..\nwidth 3\n", "unknown record"},
        {"size 2\nsize 2\n", "duplicate size"},
        {"size 2\ngrid 1 1\ngrid 1 1\n", "duplicate grid"},
        {"size 0\n", "size zero"},
        {"size 2\ngrid 0 1\n", "grid zero"},
        {"size 1\ngrid 1 1\npalette . 00000000\npalette . ffffff\n", "duplicate palette key"},
        {"size 1\ngrid 1 1\npalette ab 00000000\n", "two-character palette key"},
        {"size 1\ngrid 1 1\npalette x zzz\n", "bad colour"},
        {"size 1\ngrid 1 1\npalette x ffff\n", "colour of the wrong length"},
        {"size 2\ngrid 1 1\npalette . 00000000\ntile 0\n...\n..\n", "row too long"},
        {"size 2\ngrid 1 1\npalette . 00000000\ntile 0\n..\n", "tile short of rows"},
        {"size 1\ngrid 1 1\npalette . 00000000\ntile 0\nq\n", "character not in the palette"},
        {"size 1\ngrid 1 1\npalette . 00000000\ntile 1\n.\n", "tile index out of range"},
        {"size 1\ngrid 1 1\npalette . 00000000\ntile 0\n.\ntile 0\n.\n", "tile drawn twice"},
        {"size 1\ngrid 2 1\npalette . 00000000\ntile 0\n.\n", "a grid slot left undrawn"},
        {"size 1\ngrid 1 1\npalette . 00000000\n", "no tiles at all"},
        {"grid 1 1\n", "no size"},
        {"size 1\n", "no grid"},
        {"size 1\ngrid 1 1\npalette . 00000000\ntile\n", "tile with no index"},
        {"size 1\ngrid 1 1\ntile 0\n.\n", "row before any palette line"},
    };
    for (const Case& c : cases) {
        std::string why;
        const auto img = paint::bake_pixels(c.text, &why);
        if (img) std::printf("FAIL accepted: %s\n", c.what);
        CHECK(!img);
        // The message has to say WHERE. "line " is the whole contract.
        if (!img) CHECK(why.rfind("line ", 0) == 0);
    }
}

static void test_pixels_layout() {
    // A comment mid-tile, blank lines between tiles, and indentation: all three are
    // things a person writing sixteen tiles by hand actually does.
    const auto img = paint::bake_pixels(
        "size 2\n"
        "grid 1 2\n"
        "palette . 00000000\n"
        "palette x 010203\n"
        "tile 0   # the top one\n"
        "  xx\n"
        "  ..\n"
        "\n"
        "# and the bottom one\n"
        "tile 1\n"
        "..\n"
        "xx\n");
    CHECK(img.has_value());
    if (!img) return;
    CHECK(img->w == 2 && img->h == 4);
    CHECK(img->pixels[0] == gfx::rgba(1, 2, 3, 255));   // tile 0 row 0
    CHECK(img->pixels[2] == 0u);                        // tile 0 row 1
    CHECK(img->pixels[4] == 0u);                        // tile 1 row 0
    CHECK(img->pixels[6] == gfx::rgba(1, 2, 3, 255));   // tile 1 row 1
}

} // namespace

int main() {
    test_rect();
    test_fill();
    test_command();
    test_stroke();
    test_stroke_line();
    test_pixels_good();
    test_pixels_refusals();
    test_pixels_layout();
    if (g_failures == 0) std::printf("paint: all tests passed\n");
    else std::printf("paint: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
