// =============================================================================
//  tests/test_pixel_workspace.cpp  —  the Studio's pixel editor, driven headless
// =============================================================================
//  Same harness as test_map_workspace, because it is the same claim one workspace
//  later: the editor draws through a Renderer2D and reads a platform::InputState —
//  both plain structs — so a drag can be synthesized with no window, and the file it
//  writes can be read back and decoded.
//
//  What this file is really pinning is the round trip. An editor that paints
//  convincingly on screen and saves something the engine cannot read is worse than
//  no editor: the loss is discovered later, by a game that will not start.
//
//  Files go into a scratch directory through the assets:: seam, like test_release_ops.
// =============================================================================
#include <cstdint>
#include <cstdio>

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif
#include <filesystem>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/document/document.hpp"
#include "engine/image.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/ui.hpp"
#include "games/studio_shell/pixel_workspace.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

constexpr int CW = 400, CH = 300;
const std::string kBase  = "test_pixel_ws_tmp";
const std::string kPath  = "textures/t.hrt";
const std::string kOther = "textures/u.hrt";

constexpr gfx::Color kBg  = 0xFF203040u;
constexpr gfx::Color kFg  = 0xFFC0D0E0u;
constexpr gfx::Color kOdd = 0xFF804020u;

// 8x8, mostly kBg with a handful of kFg and one kOdd — so the sampled palette has a
// known ORDER and undo has more than one previous colour to restore.
gfx::Image fixture() {
    gfx::Image img;
    img.w = 8;
    img.h = 8;
    img.pixels.assign(64, kBg);
    for (int i = 0; i < 6; ++i) img.pixels[static_cast<std::size_t>(40 + i)] = kFg;
    img.pixels[0] = kOdd;
    return img;
}

bool write_image(const std::string& path, const gfx::Image& img) {
    return assets::write_file(path, gfx::encode_hrt(img));
}

std::optional<gfx::Image> read_image(const std::string& path) {
    auto b = assets::load_file(path);
    if (!b) return std::nullopt;
    return gfx::decode_hrt(*b);
}

// A screenshot of the editor, for a human to look at. Counting lit pixels proves
// something was drawn; only an eye can say it was the right thing.
void dump_ppm(const std::vector<std::uint32_t>& buf, const char* name) {
    std::FILE* f = std::fopen(name, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", CW, CH);
    for (std::uint32_t p : buf) {
        const unsigned char rgb[3] = {static_cast<unsigned char>((p >> 16) & 0xFF),
                                      static_cast<unsigned char>((p >> 8) & 0xFF),
                                      static_cast<unsigned char>(p & 0xFF)};
        std::fwrite(rgb, 1, 3, f);
    }
    std::fclose(f);
}

gfx::Color at(const gfx::Image& img, int x, int y) {
    return img.pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(img.w) +
                      static_cast<std::size_t>(x)];
}

// One frame: draw (which is what publishes the canvas rect) then update with `in`.
// Same order the App loop produces after the first frame.
struct Driver {
    // Parentheses, not braces: braces would build a two-element initializer_list.
    std::vector<std::uint32_t> buf = std::vector<std::uint32_t>(
        static_cast<std::size_t>(CW) * CH, 0);
    platform::Framebuffer      fb{buf.data(), CW, CH, CW};
    ui::Context                ui;

    void frame(studioshell::PixelWorkspace& ws, const platform::InputState& in,
               double dt = 1.0 / 60.0) {
        gfx::Renderer2D g(fb, 1);
        ui.begin(&g, ui::Input{}, CW, CH);
        ws.draw_canvas(ui, g, ui::Rect{0, 0, CW, CH});
        ui.end();
        ws.update(dt, in, /*interactive*/ true);
    }
};

// Mouse at the centre of image pixel (px,py). The workspace owns the pan/zoom
// arithmetic, so ask it rather than duplicating the maths — a test that recomputes
// the layout stops testing the layout.
platform::InputState at_pixel(const studioshell::PixelWorkspace& ws, int px, int py,
                              bool left_down) {
    const ui::Rect r = ws.pixel_rect(px, py);
    platform::InputState in{};
    in.mouse_x = r.x + r.w / 2;
    in.mouse_y = r.y + r.h / 2;
    in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = left_down;
    return in;
}

// ---------------------------------------------------------------------------
//  Draw, undo, save, and read the file back as an image.
// ---------------------------------------------------------------------------
void test_draw_undo_save() {
    studioshell::PixelWorkspace ws({kPath, kOther});
    CHECK(ws.loaded());
    CHECK(!ws.dirty());
    CHECK(ws.image().w == 8 && ws.image().h == 8);
    CHECK(ws.path() == kPath);

    // The palette is sampled from the image, most-used first, with the eraser at 0.
    // That ordering is the whole reason it is worth sampling: the colour you reach
    // for first is the one the sheet is mostly made of.
    CHECK(ws.palette().size() == 4);          // transparent + kBg + kFg + kOdd
    CHECK(ws.palette()[0] == 0x00000000u);
    CHECK(ws.palette()[1] == kBg);
    CHECK(ws.palette()[2] == kFg);
    CHECK(ws.colour() == kBg);                // ...and it is selected on open

    Driver d;
    d.frame(ws, platform::InputState{});      // prime the canvas rect

    CHECK(ws.palette()[3] == kOdd);

    // Drag with the selected colour, which on open is the image's MOST COMMON one —
    // so this whole gesture paints kBg over kBg and must not become an undo step.
    // A pencil that records no-ops makes Ctrl+Z appear broken, and it is the easiest
    // bug to ship because the screen looks right.
    d.frame(ws, at_pixel(ws, 1, 1, true));
    d.frame(ws, at_pixel(ws, 2, 1, true));
    d.frame(ws, at_pixel(ws, 3, 1, true));
    CHECK(at(ws.image(), 2, 1) == kBg);
    d.frame(ws, at_pixel(ws, 3, 1, false));
    CHECK(!ws.dirty());

    // Now pick a colour that differs and repeat. This is the real gesture.
    ws.register_commands();
    CHECK(cmd::exists("pixel.undo"));
    CHECK(cmd::exists("pixel.save"));

    // Use the eyedropper to select kFg from the image, exactly as a user would —
    // driving the tool rather than assigning the member, so a tool that stopped being
    // wired would fail here instead of passing.
    {
        platform::InputState pick = at_pixel(ws, 0, 5, false);   // the kFg run starts at 40 = (0,5)
        pick.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = true;
        // Switch to Pick first.
        platform::InputState key{};
        key.key_pressed[static_cast<int>(platform::Key::I)] = true;
        d.frame(ws, key);
        CHECK(ws.tool() == studioshell::PixelWorkspace::Tool::Pick);
        d.frame(ws, pick);
        CHECK(ws.colour() == kFg);
        // Picking returns to the pencil: pick-then-draw is one thought, not two.
        CHECK(ws.tool() == studioshell::PixelWorkspace::Tool::Pencil);
        CHECK(!ws.dirty());                   // and the eyedropper is not an edit
    }

    d.frame(ws, at_pixel(ws, 0, 0, true));    // starts on the kOdd pixel
    d.frame(ws, at_pixel(ws, 1, 0, true));
    d.frame(ws, at_pixel(ws, 2, 0, true));
    CHECK(at(ws.image(), 1, 0) == kFg);       // painted while the button is held
    d.frame(ws, at_pixel(ws, 2, 0, false));   // release commits the stroke
    CHECK(ws.dirty());
    CHECK(at(ws.image(), 0, 0) == kFg);
    CHECK(at(ws.image(), 3, 0) == kBg);       // and nothing beyond the drag

    // The whole drag is ONE undo step, and it restores BOTH previous colours — the
    // kOdd pixel and the kBg ones. An undo that repainted one uniform colour would
    // pass a weaker test and lose work.
    CHECK(cmd::run("pixel.undo").ok);
    CHECK(at(ws.image(), 0, 0) == kOdd);
    CHECK(at(ws.image(), 1, 0) == kBg);
    CHECK(!ws.dirty());                       // back at the saved point
    CHECK(cmd::run("pixel.redo").ok);
    CHECK(at(ws.image(), 0, 0) == kFg);

    // Save, then read the FILE back and decode it. This is the round trip: an editor
    // that writes something the engine cannot read is worse than no editor.
    CHECK(cmd::run("pixel.save").ok);
    CHECK(!ws.dirty());
    const auto on_disk = read_image(kPath);
    CHECK(on_disk.has_value());
    if (on_disk) {
        CHECK(on_disk->w == 8 && on_disk->h == 8);
        CHECK(at(*on_disk, 0, 0) == kFg);
        CHECK(on_disk->pixels == ws.image().pixels);
    }
    // A real save clears the autosave, or the next open would offer to recover content
    // the file already holds — which teaches the user to dismiss the prompt. "Clears"
    // means TRUNCATES: the assets seam has no delete on purpose, and doc::open treats
    // an empty autosave as absent, so the two are indistinguishable to every reader.
    const auto leftover = assets::load_file(doc::autosave_path(kPath));
    CHECK(!leftover || leftover->empty());
}

// ---------------------------------------------------------------------------
//  Erase writes transparency, and the round trip preserves alpha.
// ---------------------------------------------------------------------------
void test_erase_alpha() {
    studioshell::PixelWorkspace ws({kPath});
    CHECK(ws.loaded());
    Driver d;
    d.frame(ws, platform::InputState{});

    platform::InputState in = at_pixel(ws, 4, 4, false);
    in.mouse_down[static_cast<int>(platform::MouseButton::Right)] = true;
    d.frame(ws, in);
    in.mouse_down[static_cast<int>(platform::MouseButton::Right)] = false;
    d.frame(ws, in);

    CHECK(at(ws.image(), 4, 4) == 0x00000000u);
    CHECK(ws.dirty());
    CHECK(ws.save().ok);
    const auto on_disk = read_image(kPath);
    CHECK(on_disk.has_value());
    // Alpha survives encode/decode. `.hrt` is RGBA8 with no premultiplication, so a
    // fully transparent pixel must come back as exactly zero rather than as black.
    if (on_disk) CHECK(at(*on_disk, 4, 4) == 0x00000000u);
}

// ---------------------------------------------------------------------------
//  Switching texture, and the refusal that protects unsaved work.
// ---------------------------------------------------------------------------
void test_switch_texture() {
    studioshell::PixelWorkspace ws({kPath, kOther});
    CHECK(ws.count() == 2);
    CHECK(ws.index() == 0);
    Driver d;
    d.frame(ws, platform::InputState{});

    ws.register_commands();
    CHECK(cmd::exists("pixel.next"));

    // Dirty the document, then ask to switch. The switch must be REFUSED: reloading
    // over unsaved pixels is a silent loss, and there is no undo across a reload.
    platform::InputState in = at_pixel(ws, 2, 2, false);
    in.mouse_down[static_cast<int>(platform::MouseButton::Right)] = true;
    d.frame(ws, in);
    in.mouse_down[static_cast<int>(platform::MouseButton::Right)] = false;
    d.frame(ws, in);
    CHECK(ws.dirty());

    const engine::OpResult refused = cmd::run("pixel.next");
    CHECK(!refused.ok);
    CHECK(refused.message.find("save or undo") != std::string::npos);
    CHECK(ws.index() == 0);                   // ...and it really did not move

    // Save, and now it moves. Going through the COMMAND rather than a setter is the
    // point: the inspector's list and the palette entry are one operation, so a switch
    // that stopped being wired could not pass here.
    CHECK(ws.save().ok);
    CHECK(cmd::run("pixel.next").ok);
    CHECK(ws.index() == 1);
    CHECK(ws.path() == kOther);
    CHECK(cmd::run("pixel.next").ok);         // wraps
    CHECK(ws.index() == 0);
    cmd::clear();
}

// ---------------------------------------------------------------------------
//  Autosave and recovery, on a binary document.
// ---------------------------------------------------------------------------
void test_recovery() {
    // Put an autosave beside the file whose content differs from it.
    gfx::Image changed = fixture();
    changed.pixels[10] = 0xFFFF0000u;
    const std::vector<std::uint8_t> bytes = gfx::encode_hrt(changed);
    CHECK(doc::write_autosave(kPath, std::string(bytes.begin(), bytes.end())));

    studioshell::PixelWorkspace ws({kPath});
    CHECK(ws.loaded());
    CHECK(ws.recovery_pending());             // offered, NOT applied
    CHECK(at(ws.image(), 2, 1) != 0xFFFF0000u);

    ws.take_recovery();
    CHECK(!ws.recovery_pending());
    CHECK(at(ws.image(), 2, 1) == 0xFFFF0000u);
    // Recovery is an EDIT: the document is dirty (the file still holds the older
    // version) and one undo returns to what was deliberately saved.
    CHECK(ws.dirty());
    CHECK(ws.image().pixels != fixture().pixels);
    doc::discard_autosave(kPath);
}

// ---------------------------------------------------------------------------
//  Nothing to open is EXPLAINED, not blank.
// ---------------------------------------------------------------------------
void test_no_texture() {
    studioshell::PixelWorkspace none({});
    CHECK(!none.loaded());
    CHECK(none.problem().find("asset texture") != std::string::npos);
    CHECK(!none.save().ok);                   // and saving nothing is refused

    studioshell::PixelWorkspace missing({"textures/nope.hrt"});
    CHECK(!missing.loaded());
    CHECK(missing.problem().find("cannot read") != std::string::npos);

    // A file that exists and is not an image says which of the two it is.
    CHECK(assets::write_file("textures/bad.hrt", std::vector<std::uint8_t>{'n', 'o'}));
    studioshell::PixelWorkspace bad({"textures/bad.hrt"});
    CHECK(!bad.loaded());
    CHECK(bad.problem().find("not a .hrt") != std::string::npos);
}

// ---------------------------------------------------------------------------
//  The project's REAL sheet, not a fixture.
//
//  A fixture proves the fixture. Kenney's tilemap is 192x176 — a sheet, not a tile —
//  and it is the file a user would actually open first, so the layout, the sampled
//  palette and the tile guide have to survive it. Read-only: this one never saves.
// ---------------------------------------------------------------------------
void test_real_sheet() {
    assets::set_base_path(ASSET_ROOT "/assets");
    studioshell::PixelWorkspace ws({"textures/town.hrt", "textures/farm_water.hrt"});
    CHECK(ws.loaded());
    CHECK(ws.image().w == 192 && ws.image().h == 176);

    // The palette fills up on a real sheet — a flat-colour pack has far more than the
    // fifteen it keeps, so this is the cap doing its job rather than a small image.
    CHECK(ws.palette().size() == 16);
    CHECK(ws.palette()[0] == 0x00000000u);
    // ...and the pack IS mostly transparent, so the eraser swatch is not cosmetic:
    // the most common colour is excluded from the ranking and still shown at 0.
    CHECK(ws.colour() != 0x00000000u);

    Driver d;
    d.frame(ws, platform::InputState{});
    d.frame(ws, platform::InputState{});
    // Something was actually drawn. An editor whose canvas renders empty passes every
    // state assertion above.
    int lit = 0;
    for (std::uint32_t p : d.buf) if ((p & 0x00FFFFFFu) != 0) ++lit;
    CHECK(lit > CW * CH / 10);

    dump_ppm(d.buf, "pixel_workspace.ppm");
    CHECK(!ws.dirty());       // opening and looking is not an edit
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / kBase;
    fs::remove_all(root);
    fs::create_directories(root / "textures");
    assets::set_base_path(root.string());

    CHECK(write_image(kPath, fixture()));
    CHECK(write_image(kOther, fixture()));

    test_draw_undo_save();
    cmd::clear();
    // Restore the fixture between cases: each one below assumes the file it opens is
    // the one written above, and a test that depends on the previous test's leftovers
    // is a test that passes in the wrong order and fails alone.
    CHECK(write_image(kPath, fixture()));
    test_erase_alpha();
    CHECK(write_image(kPath, fixture()));
    test_switch_texture();
    CHECK(write_image(kPath, fixture()));
    test_recovery();
    CHECK(write_image(kPath, fixture()));
    test_no_texture();

    fs::remove_all(root);
    test_real_sheet();          // last: it repoints the asset root at the repository
    if (g_failures == 0) std::printf("pixel_workspace: all tests passed\n");
    else std::printf("pixel_workspace: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
