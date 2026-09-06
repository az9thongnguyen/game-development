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
#include <cmath>
#include <cstdint>
#include <cstdio>

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif
#include <filesystem>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/commands/asset_commands.hpp"
#include "engine/commands/registry.hpp"
#include "engine/document/document.hpp"
#include "engine/image.hpp"
#include "engine/paint/colour.hpp"
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
void dump_ppm(const std::vector<std::uint32_t>& buf, int w, int h, const char* name) {
    std::FILE* f = std::fopen(name, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
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
    int                        w, h;
    std::vector<std::uint32_t> buf;
    platform::Framebuffer      fb;
    ui::Context                ui;

    explicit Driver(int w_ = CW, int h_ = CH)
        : w(w_), h(h_),
          // Parentheses, not braces: braces would build a two-element initializer_list.
          buf(static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_), 0),
          fb{buf.data(), w_, h_, w_} {}

    void frame(studioshell::PixelWorkspace& ws, const platform::InputState& in,
               double dt = 1.0 / 60.0) {
        gfx::Renderer2D g(fb, 1);
        ui.begin(&g, ui::Input{}, w, h);
        ws.draw_canvas(ui, g, ui::Rect{0, 0, w, h});
        ui.end();
        ws.update(dt, in, /*interactive*/ true);
    }

    // A frame that draws the INSPECTOR too, split the way WorkspaceHost splits it, and
    // with a real ui::Input so the panel's own controls can be pressed. Two inputs
    // because the workspace has two: the mouse the widgets see and the keyboard the
    // editor sees, and half of this slice is about which of them owns a letter.
    void panel(studioshell::PixelWorkspace& ws, const ui::Input& uin,
               const platform::InputState& in = platform::InputState{},
               double dt = 1.0 / 60.0) {
        const int       iw = ws.inspector_width();
        gfx::Renderer2D g(fb, 1);
        ui.begin(&g, uin, w, h);
        ws.draw_canvas(ui, g, ui::Rect{0, 0, w - iw, h});
        ws.draw_inspector(ui, g, ui::Rect{w - iw, 0, iw, h});
        ui.end();
        ws.update(dt, in, /*interactive*/ true);
    }
};

// The panel needs more room than the 400x300 canvas cases: an inspector is 280 wide
// and its content is taller than 300, which is a fact one of the tests below is about.
constexpr int PW = 700, PH = 620;

// A press, then the frames that hold it. `pressed` is one frame; `down` is the drag.
ui::Input mouse(int x, int y, bool down, bool pressed) {
    ui::Input u{};
    u.mx = x;
    u.my = y;
    u.down = down;
    u.pressed = pressed;
    return u;
}

// A RELEASE over (x,y). `ui::interact` activates a button on release-over, so a test
// that only ever presses proves the widget was drawn and never that it can be
// pressed — which is the ch.126 blind spot, and was true of every button in this file
// until this helper existed.
ui::Input release(int x, int y) {
    ui::Input u{};
    u.mx = x;
    u.my = y;
    u.released = true;
    return u;
}

// Press then release over the centre of `r`, with a frame either side so update()
// sees the intent the same way it does from a real hand.
void click(Driver& d, studioshell::PixelWorkspace& ws, ui::Rect r) {
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    d.panel(ws, mouse(cx, cy, true, true));
    d.panel(ws, release(cx, cy));
    d.panel(ws, ui::Input{});
}

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
//  A sheet that did not exist. The ceiling chapter 127 recorded: this workspace
//  could change art and could not ADD any, so every new tile still began in a text
//  editor with three files to remember.
//
//  Driven through the CONTROL, not only through the function. Chapter 126's lesson
//  is that every input test asks "can this be pressed" and none asks whether a drawn
//  control is pressable — so the last block here types into the real field and clicks
//  the real rect the inspector published.
// ---------------------------------------------------------------------------
void test_new_sheet() {
    cmd::clear();
    cmd::register_asset_commands();          // the operation the trigger calls

    static const char kManifest[] = "gameproject1\nname T\nschema 1\nentry fps\n";
    CHECK(assets::write_file("projects/t.gameproject",
                             std::vector<std::uint8_t>(kManifest, kManifest + sizeof(kManifest) - 1)));

    studioshell::PixelWorkspace ws({kPath}, "projects/t.gameproject");
    ws.register_commands();
    CHECK(cmd::exists("pixel.new"));
    Driver d(PW, PH);
    d.panel(ws, ui::Input{});                // publish the layout

    // The name is a path. The refusal lives in asset.new and this must inherit it
    // rather than re-implement a weaker version of it.
    CHECK(!ws.new_sheet("../evil").ok);
    CHECK(!ws.new_sheet("").ok);
    CHECK(ws.count() == 1);

    // ---- refused while dirty, and it really wrote nothing ----
    platform::InputState in = at_pixel(ws, 2, 2, false);
    in.mouse_down[static_cast<int>(platform::MouseButton::Right)] = true;
    d.frame(ws, in);
    in.mouse_down[static_cast<int>(platform::MouseButton::Right)] = false;
    d.frame(ws, in);
    CHECK(ws.dirty());

    const engine::OpResult refused = cmd::run("pixel.new", {"signs"});
    CHECK(!refused.ok);
    CHECK(refused.message.find("save or undo") != std::string::npos);
    CHECK(ws.count() == 1);
    CHECK(!assets::load_file("textures/signs.pix"));   // a refusal that half-happened
    CHECK(!assets::load_file("textures/signs.hrt"));   // is the bug this pins

    // ---- the guard's OTHER direction: after a save it goes through ----
    CHECK(ws.save().ok);
    const engine::OpResult made = cmd::run("pixel.new", {"signs"});
    CHECK(made.ok);
    if (!made.ok) std::printf("      %s\n", made.message.c_str());
    CHECK(ws.count() == 2);
    CHECK(ws.index() == 1);                            // and it OPENED the new one
    CHECK(ws.path() == "textures/signs.hrt");
    CHECK(ws.image().w == 16 && ws.image().h == 16);
    for (gfx::Color c : ws.image().pixels) CHECK((c >> 24) == 0);
    CHECK(!ws.dirty());                                // a fresh sheet is not unsaved work
    CHECK(assets::load_file("textures/signs.pix").has_value());   // born as a SOURCE

    // Declared, or the project cannot see it.
    const auto mf = assets::load_file("projects/t.gameproject");
    CHECK(mf.has_value());
    if (mf) CHECK(std::string(mf->begin(), mf->end()).find("asset texture textures/signs.hrt") !=
                  std::string::npos);

    // ---- drawn where it can be pressed ----
    d.panel(ws, ui::Input{});
    const ui::Rect field = ws.new_name_rect();
    const ui::Rect btn   = ws.new_button_rect();
    CHECK(field.w > 0 && field.h > 0);
    CHECK(btn.w > 0 && btn.h > 0);
    CHECK(btn.x >= field.x + field.w);                 // side by side, not stacked on top
    CHECK(btn.x + btn.w <= PW);                        // and inside the panel

    // Type a name into the real field, then click the real button. Nothing below
    // touches new_sheet() directly: if the Create button ever stops being wired, or
    // is drawn somewhere the hit test does not look, this is what goes red.
    const int fx = field.x + field.w / 2, fy = field.y + field.h / 2;
    d.panel(ws, mouse(fx, fy, true, true));
    const std::string name = "fence";
    ui::Input typing = mouse(fx, fy, false, false);
    typing.text     = name.c_str();
    typing.text_len = name.size();
    d.panel(ws, typing);
    CHECK(ws.new_name() == name);

    click(d, ws, btn);
    // A frame to look at: three chapters running, the bug was one only a screenshot
    // could show. Counting pixels proves something was drawn; an eye says what.
    dump_ppm(d.buf, d.w, d.h, "pixel_new_sheet.ppm");
    CHECK(ws.count() == 3);
    CHECK(ws.path() == "textures/fence.hrt");
    CHECK(ws.new_name().empty());                      // the field clears on success
    CHECK(assets::load_file("textures/fence.hrt").has_value());

    // ---- the button's OTHER direction: disabled means it does nothing ----
    // Testing that new_sheet() refuses is not the same as testing that the CONTROL
    // refuses. A button drawn enabled over a function that says no is chapter 126
    // wearing a different hat, and only pressing it can tell the two apart.
    {
        d.panel(ws, ui::Input{});
        const std::size_t before = ws.count();
        CHECK(ws.new_name().empty());
        while (ws.take_message()) {}                 // drain, so the next one is new
        click(d, ws, ws.new_button_rect());          // no name typed
        CHECK(ws.count() == before);
        // Nothing HAPPENED, and nothing was SAID. That second half is the assertion:
        // the function refuses a blank name anyway, so a button wrongly drawn enabled
        // would still create nothing — it would just answer with an error nobody
        // asked for. Silence is the only observable difference between a disabled
        // control and an enabled one over a guard.
        CHECK(!ws.take_message().has_value());

        // Now with a name, but with unsaved pixels: still nothing.
        const ui::Rect f2 = ws.new_name_rect();
        d.panel(ws, mouse(f2.x + f2.w / 2, f2.y + f2.h / 2, true, true));
        const std::string n2 = "gate";
        ui::Input typing2 = mouse(f2.x + f2.w / 2, f2.y + f2.h / 2, false, false);
        typing2.text     = n2.c_str();
        typing2.text_len = n2.size();
        d.panel(ws, typing2);
        CHECK(ws.new_name() == n2);

        platform::InputState dirt = at_pixel(ws, 3, 3, false);
        dirt.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
        d.frame(ws, dirt);
        dirt.mouse_down[static_cast<int>(platform::MouseButton::Left)] = false;
        d.frame(ws, dirt);
        CHECK(ws.dirty());
        d.panel(ws, ui::Input{});
        while (ws.take_message()) {}
        click(d, ws, ws.new_button_rect());
        CHECK(ws.count() == before);                 // still refused, from the control
        CHECK(!ws.take_message().has_value());       // ...and silently, see above
        CHECK(!assets::load_file("textures/gate.hrt"));

        // ...and once saved, the SAME click goes through. Without this line the two
        // checks above would pass on a button that is never enabled at all.
        CHECK(ws.save().ok);
        d.panel(ws, ui::Input{});
        click(d, ws, ws.new_button_rect());
        CHECK(ws.count() == before + 1);
        CHECK(assets::load_file("textures/gate.hrt").has_value());
    }

    // While the harness can finally press a button: Save was drawn in every frame of
    // every test in this file and clicked in none of them. Pressing it is the only
    // thing that would notice it being drawn somewhere the hit test does not look.
    in = at_pixel(ws, 1, 1, false);
    in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
    d.frame(ws, in);
    in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = false;
    d.frame(ws, in);
    CHECK(ws.dirty());
    d.panel(ws, ui::Input{});                 // publish the layout with Save enabled
    const ui::Rect save = ws.save_rect();
    CHECK(save.w > 0 && save.h > 0);
    click(d, ws, save);
    CHECK(!ws.dirty());                       // the click reached the operation

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

    dump_ppm(d.buf, d.w, d.h, "pixel_workspace.ppm");
    CHECK(!ws.dirty());       // opening and looking is not an edit
}


// ---------------------------------------------------------------------------
//  The ceiling this workspace shipped with.
//
//  Both doors into `colour_` read the FILE: the palette is the image's own most-used
//  colours and the eyedropper is a pixel. So every colour the editor could paint was
//  one the sheet already had, and it could not introduce a single new hue — a
//  retouching tool, not a drawing one. Typing a code is the exact door out.
// ---------------------------------------------------------------------------
void test_mix_by_code() {
    constexpr gfx::Color kNew = 0xFF3C7A2Eu;   // a green this fixture has never held

    studioshell::PixelWorkspace ws({kPath});
    CHECK(ws.loaded());
    Driver d(PW, PH);
    d.panel(ws, ui::Input{});                  // publish the layout

    const ui::Rect f = ws.hex_rect();
    CHECK(f.w > 0 && f.h > 0);
    // The field opens showing the selected colour, so it can be read as well as typed.
    CHECK(ws.hex_field() == "#FF203040");

    const int fx = f.x + f.w / 2, fy = f.y + f.h / 2;
    d.panel(ws, mouse(fx, fy, true, true));    // focus: selects the whole value

    // Half a code must leave the colour ALONE. The caller is a field being typed into,
    // and a parser that guessed would repaint the brush on every keystroke.
    const std::string half = "#FF3C";
    ui::Input         typing = mouse(fx, fy, false, false);
    typing.text     = half.c_str();
    typing.text_len = half.size();
    d.panel(ws, typing);
    CHECK(ws.hex_field() == half);
    CHECK(ws.colour() == 0xFF203040u);         // ...unchanged

    const std::string code = "#FF3C7A2E";
    typing.text     = code.c_str();
    typing.text_len = code.size();
    ui::Input select_all = typing;
    select_all.keys.select_all = true;         // replace, as a user retyping would
    d.panel(ws, select_all);
    CHECK(ws.hex_field() == code);
    CHECK(ws.colour() == kNew);
    // The mixer moved with it. A typed colour that left the sliders behind would make
    // the next drag jump to whatever was selected before.
    CHECK(paint::from_hsv(ws.mix(), 0xFF) == kNew);

    // A shorter spelling of the same colour must not be rewritten under the caret.
    // "3C7A2E" is #FF3C7A2E, and canonicalising the field the moment it parses would
    // move the text six characters while the user is still typing in it.
    const std::string short_form = "3C7A2E";
    typing.text     = short_form.c_str();
    typing.text_len = short_form.size();
    ui::Input select_short = typing;
    select_short.keys.select_all = true;
    d.panel(ws, select_short);
    CHECK(ws.hex_field() == short_form);
    CHECK(ws.colour() == kNew);

    // ...and it really is a colour NEITHER of the old doors could have reached.
    bool in_palette = false, in_image = false;
    for (gfx::Color c : ws.palette()) in_palette |= (c == kNew);
    for (gfx::Color c : ws.image().pixels) in_image |= (c == kNew);
    CHECK(!in_palette);
    CHECK(!in_image);

    // Paint with it, and the round trip that matters: the file now holds a colour the
    // file never held. That is the whole slice, in one assertion.
    platform::InputState draw = at_pixel(ws, 3, 3, true);
    d.panel(ws, ui::Input{}, draw);
    draw.mouse_down[static_cast<int>(platform::MouseButton::Left)] = false;
    d.panel(ws, ui::Input{}, draw);
    CHECK(at(ws.image(), 3, 3) == kNew);
    CHECK(ws.save().ok);
    const auto on_disk = read_image(kPath);
    CHECK(on_disk.has_value());
    if (on_disk) CHECK(at(*on_disk, 3, 3) == kNew);

    // The eyedropper feeds the field too: every way of choosing a colour goes through
    // one adopt(), so what the panel shows can never disagree with what the brush is.
    // Click the canvas first: the field still has the keyboard, and a letter shortcut
    // belongs to it until the user clicks away.
    d.panel(ws, mouse(20, 20, true, true));
    platform::InputState key{};
    key.key_pressed[static_cast<int>(platform::Key::I)] = true;
    d.panel(ws, ui::Input{}, key);
    platform::InputState pick = at_pixel(ws, 0, 5, false);   // a kFg pixel
    pick.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = true;
    d.panel(ws, ui::Input{}, pick);
    CHECK(ws.colour() == kFg);
    CHECK(ws.hex_field() == "#FFC0D0E0");
}

// ---------------------------------------------------------------------------
//  Why the mixer holds coordinates: a drag through black has to be reversible.
//
//  test_paint proves the arithmetic. This proves the WIRING — that the workspace kept
//  the sliders' own state instead of re-reading the colour, which is the version that
//  looks identical until somebody drags value to the bottom.
// ---------------------------------------------------------------------------
void test_mix_by_slider() {
    studioshell::PixelWorkspace ws({kPath});
    Driver d(PW, PH);
    d.panel(ws, ui::Input{});

    const ui::Rect v = ws.mix_slider(2);       // 0 = hue, 1 = sat, 2 = value
    CHECK(v.w > 0 && v.h > 0);
    CHECK(ws.mix_slider(0).y < ws.mix_slider(1).y);
    CHECK(ws.mix_slider(1).y < v.y);
    CHECK(ws.mix_slider(9).w == 0);            // out of range is empty, not a crash

    // The colour on open is the sheet's most common one: hue 210, half saturated.
    CHECK(ws.colour() == kBg);
    CHECK(std::abs(ws.mix().h - 210.0f) < 0.5f);

    const int my = v.y + v.h / 2;
    d.panel(ws, mouse(v.x + v.w / 2, my, true, true));      // grab the knob
    d.panel(ws, mouse(v.x - 50, my, true, false));          // drag past the left end
    CHECK(ws.colour() == 0xFF000000u);                      // value 0 is black
    // Black remembers nothing. If the workspace re-derived the sliders from the
    // colour here, hue and saturation would now be 0 and dragging back would give
    // WHITE. The assertion below is the difference between the two implementations.
    d.panel(ws, mouse(v.x + v.w + 50, my, true, false));    // drag past the right end
    CHECK(ws.colour() != 0xFFFFFFFFu);
    CHECK(ws.colour() == 0xFF80BFFFu);                      // hue 210, sat 0.5, value 1
    CHECK(std::abs(ws.mix().h - 210.0f) < 0.5f);
    d.panel(ws, mouse(v.x + v.w + 50, my, false, false));   // release

    // Alpha rides alongside rather than inside: a picked half-transparent pixel keeps
    // its alpha while its hue is dragged.
    ws.reload();
    d.panel(ws, ui::Input{});
    platform::InputState key{};
    key.key_pressed[static_cast<int>(platform::Key::I)] = true;
    d.panel(ws, ui::Input{}, key);
    // Make a translucent pixel to pick, the only way the workspace offers: type it.
    const ui::Rect f = ws.hex_rect();
    d.panel(ws, mouse(f.x + 4, f.y + f.h / 2, true, true));
    const std::string code = "#80C08040";
    ui::Input         typing = mouse(f.x + 4, f.y + f.h / 2, false, false);
    typing.text     = code.c_str();
    typing.text_len = code.size();
    typing.keys.select_all = true;
    d.panel(ws, typing);
    CHECK(ws.colour() == 0x80C08040u);
    const ui::Rect hue = ws.mix_slider(0);
    d.panel(ws, mouse(hue.x + hue.w / 2, hue.y + hue.h / 2, true, true));
    d.panel(ws, mouse(hue.x + hue.w + 50, hue.y + hue.h / 2, true, false));
    CHECK(gfx::a_of(ws.colour()) == 0x80);     // the hue moved; the alpha did not
}

// ---------------------------------------------------------------------------
//  B, R, G and I are tools. They are also hex digits.
//
//  The editor's letter shortcuts and the code field want the same keys, and the field
//  is the one that must win while it has the keyboard — otherwise typing a brown
//  (#8B5A2B) switches the tool twice on the way through.
// ---------------------------------------------------------------------------
void test_field_owns_the_letters() {
    studioshell::PixelWorkspace ws({kPath});
    Driver d(PW, PH);
    d.panel(ws, ui::Input{});

    platform::InputState r_key{};
    r_key.key_pressed[static_cast<int>(platform::Key::R)] = true;
    d.panel(ws, ui::Input{}, r_key);
    CHECK(ws.tool() == studioshell::PixelWorkspace::Tool::Rect);

    const ui::Rect f = ws.hex_rect();
    d.panel(ws, mouse(f.x + 4, f.y + f.h / 2, true, true));   // the field takes focus

    const std::string b = "B";
    ui::Input         typing = mouse(f.x + 4, f.y + f.h / 2, false, false);
    typing.text     = b.c_str();
    typing.text_len = b.size();
    typing.keys.select_all = true;
    platform::InputState b_key{};
    b_key.key_pressed[static_cast<int>(platform::Key::B)] = true;
    d.panel(ws, typing, b_key);
    CHECK(ws.hex_field() == "B");                             // the letter went here
    CHECK(ws.tool() == studioshell::PixelWorkspace::Tool::Rect);   // ...and only here

    // And the shortcut comes BACK when focus leaves. A guard that never lifts is the
    // same bug in the other direction, and it is the half nobody tests.
    //
    // Clicking the CANVAS is the case that matters, and it did not work when this was
    // first written: ui::Context only moved focus when another WIDGET took it, so a
    // press on empty space left the field holding the keyboard and every letter
    // shortcut in the editor stayed dead for the rest of the session. The fix is in
    // ui::Context::end(), because it is not this workspace's bug — it is what
    // clicking outside a text field means anywhere.
    d.panel(ws, mouse(20, 20, true, true));                   // the canvas: no widget
    d.panel(ws, mouse(20, 20, false, false), b_key);
    CHECK(ws.tool() == studioshell::PixelWorkspace::Tool::Pencil);

    // ...and a click on another WIDGET moves the keyboard too, not just clears it.
    platform::InputState r2{};
    r2.key_pressed[static_cast<int>(platform::Key::R)] = true;
    d.panel(ws, ui::Input{}, r2);
    CHECK(ws.tool() == studioshell::PixelWorkspace::Tool::Rect);
    const ui::Rect f2 = ws.hex_rect();
    d.panel(ws, mouse(f2.x + 4, f2.y + f2.h / 2, true, true));
    const ui::Rect v = ws.mix_slider(2);
    d.panel(ws, mouse(v.x + v.w / 2, v.y + v.h / 2, true, true));
    d.panel(ws, mouse(v.x + v.w / 2, v.y + v.h / 2, false, false), b_key);
    CHECK(ws.tool() == studioshell::PixelWorkspace::Tool::Pencil);
}

// ---------------------------------------------------------------------------
//  A panel with no room left does not overflow — it DISAPPEARS.
//
//  `ui::Context::slot` clamps to what is left, so a control that does not fit gets a
//  rect of height 0: drawn as nothing, hit as nothing, reported by nothing. Adding a
//  colour mixer made the inspector ~130 logical pixels taller, which is exactly the
//  change that finds this. The workspace asks for the height it wanted and compares.
// ---------------------------------------------------------------------------
void test_inspector_clipped() {
    studioshell::PixelWorkspace ws({kPath, kOther});

    Driver tall(PW, PH);
    tall.panel(ws, ui::Input{});
    CHECK(!ws.inspector_clipped());
    CHECK(ws.status().find("clipped") == std::string::npos);

    Driver squat(PW, 240);
    squat.panel(ws, ui::Input{});
    CHECK(ws.inspector_clipped());
    // Said on the status line, which is OUTSIDE the panel — when the panel is too
    // short there is by definition no room inside it to say so.
    CHECK(ws.status().find("clipped") != std::string::npos);

    // A screenshot of the whole panel, for a human: the sliders, the preview and the
    // field are geometry a passing assertion cannot see.
    Driver shot(PW, PH);
    shot.panel(ws, ui::Input{});
    shot.panel(ws, ui::Input{});
    dump_ppm(shot.buf, shot.w, shot.h, "pixel_mixer.ppm");
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
    CHECK(write_image(kPath, fixture()));
    test_mix_by_code();
    CHECK(write_image(kPath, fixture()));
    test_mix_by_slider();
    CHECK(write_image(kPath, fixture()));
    test_field_owns_the_letters();
    CHECK(write_image(kPath, fixture()));
    test_inspector_clipped();
    CHECK(write_image(kPath, fixture()));
    test_new_sheet();

    fs::remove_all(root);
    test_real_sheet();          // last: it repoints the asset root at the repository
    if (g_failures == 0) std::printf("pixel_workspace: all tests passed\n");
    else std::printf("pixel_workspace: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
