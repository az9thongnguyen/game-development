// =============================================================================
//  tests/test_map_workspace.cpp  —  the Studio's map editor, driven headless
// =============================================================================
//  The workspace is the piece that finally CONNECTS the map format, the undo stack,
//  the autosave and the command registry. It draws through a Renderer2D and reads a
//  platform::InputState — both plain structs — so the whole thing runs with no
//  window: synthesize a drag, assert the document changed, undo it, save it, and
//  reopen it with an autosave beside it.
//
//  Files go into a scratch directory through the assets:: seam, like test_release_ops.
// =============================================================================
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/document/document.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/ui.hpp"
#include "games/fps/map.hpp"
#include "games/studio_shell/map_workspace.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

constexpr int CW = 400, CH = 300;          // canvas framebuffer
const std::string kBase = "test_map_ws_tmp";
const std::string kPath = "maps/t.map";

std::string fixture_text() {
    tilemap::Map m;
    m.name = "fixture"; m.w = 8; m.h = 6; m.tile = 16;
    tilemap::Layer ground;
    ground.name = "ground"; ground.kind = tilemap::LayerKind::Tiles;
    ground.cells.assign(48, 0);
    tilemap::Layer collide;
    collide.name = "collide"; collide.kind = tilemap::LayerKind::Mask;
    collide.cells.assign(48, 0);
    m.layers.push_back(std::move(ground));
    m.layers.push_back(std::move(collide));
    return tilemap::to_text(m);
}

bool write_text(const std::string& path, const std::string& text) {
    return assets::write_file(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

std::string read_text(const std::string& path) {
    auto b = assets::load_file(path);
    return b ? std::string(b->begin(), b->end()) : std::string();
}

// A screenshot, for a human to look at. Counting pixels proves something was drawn;
// only an eye can say it was the right thing — which is how the last four chapters
// each found a bug no assertion had.
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

// One frame: draw (which is what publishes the canvas rect) then update with `in`.
// Same order the App loop produces after the first frame.
struct Driver {
    // Parentheses, not braces: braces would build a two-element initializer_list.
    std::vector<std::uint32_t> buf = std::vector<std::uint32_t>(
        static_cast<std::size_t>(CW) * CH, 0);
    platform::Framebuffer      fb{buf.data(), CW, CH, CW};
    ui::Context                ui;

    void frame(studioshell::MapWorkspace& ws, const platform::InputState& in, double dt = 1.0 / 60.0) {
        gfx::Renderer2D g(fb, 1);
        ui.begin(&g, ui::Input{}, CW, CH);
        ws.draw_canvas(ui, g, ui::Rect{0, 0, CW, CH});
        ui.end();
        ws.update(dt, in, /*interactive*/ true);
    }

    // Canvas AND inspector, split the way WorkspaceHost splits them — the only way to
    // see a panel section at all, and this file had no way to draw one.
    void panel(studioshell::MapWorkspace& ws, const platform::InputState& in = {},
               double dt = 1.0 / 60.0) {
        const int       iw = ws.inspector_width();
        gfx::Renderer2D g(fb, 1);
        ui.begin(&g, ui::Input{}, CW, CH);
        ws.draw_canvas(ui, g, ui::Rect{0, 0, CW - iw, CH});
        ws.draw_inspector(ui, g, ui::Rect{CW - iw, 0, iw, CH});
        ui.end();
        ws.update(dt, in, /*interactive*/ true);
    }
};

// A framebuffer big enough to hold a map AND a whole inspector, for screenshots.
constexpr int SW = 900, SH = 700;
struct ShotDriver {
    std::vector<std::uint32_t> buf = std::vector<std::uint32_t>(
        static_cast<std::size_t>(SW) * SH, 0);
    platform::Framebuffer      fb{buf.data(), SW, SH, SW};
    ui::Context                ui;

    void panel(studioshell::MapWorkspace& ws, double dt = 1.0 / 60.0) {
        panel(ws, ui::Input{}, platform::InputState{}, dt);
    }

    // The widgets read ui::Input and the canvas reads platform::InputState — two
    // input paths through one frame, and a test that fed only one of them would press
    // nothing while looking like it had.
    void panel(studioshell::MapWorkspace& ws, const ui::Input& uin,
               const platform::InputState& in = platform::InputState{},
               double dt = 1.0 / 60.0) {
        const int       iw = ws.inspector_width();
        gfx::Renderer2D g(fb, 1);
        ui.begin(&g, uin, SW, SH);
        ws.draw_canvas(ui, g, ui::Rect{0, 0, SW - iw, SH});
        ws.draw_inspector(ui, g, ui::Rect{SW - iw, 0, iw, SH});
        ui.end();
        ws.update(dt, in, /*interactive*/ true);
    }
};

// The size `--lab map` opens at. Separate from ShotDriver's 900x700 because "it fits
// in the screenshot" and "it fits in the window" are two different claims.
struct LabDriver {
    static constexpr int LW = 960, LH = 600;
    std::vector<std::uint32_t> buf = std::vector<std::uint32_t>(
        static_cast<std::size_t>(LW) * LH, 0);
    platform::Framebuffer      fb{buf.data(), LW, LH, LW};
    ui::Context                ui;

    void panel(studioshell::MapWorkspace& ws) {
        // The pad and status strip WorkspaceHost reserves, so this is the rect the
        // real host hands over rather than the whole window.
        const int pad = 16, status = 24;
        const int iw = ws.inspector_width();
        gfx::Renderer2D g(fb, 1);
        ui.begin(&g, ui::Input{}, LW, LH);
        const ui::Rect body{pad, pad, LW - pad * 2, LH - pad * 2 - status};
        ws.draw_canvas(ui, g, ui::Rect{body.x, body.y, body.w - iw - 12, body.h});
        ws.draw_inspector(ui, g, ui::Rect{body.x + body.w - iw, body.y, iw, body.h});
        ui.end();
        ws.update(1.0 / 60.0, platform::InputState{}, /*interactive*/ true);
    }
};

// ui::interact fires on RELEASE over the rect, so a test that only presses proves a
// widget was drawn and never that it can be pressed (chapter 133's lesson, and 126's).
void click(ShotDriver& d, studioshell::MapWorkspace& ws, ui::Rect r) {
    if (r.w <= 0 || r.h <= 0) return;
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    ui::Input press{}; press.mx = cx; press.my = cy; press.down = true; press.pressed = true;
    ui::Input rel{};   rel.mx = cx;   rel.my = cy;   rel.released = true;
    d.panel(ws, press);
    d.panel(ws, rel);
    d.panel(ws);
}

platform::InputState key_press(platform::Key k) {
    platform::InputState in{};
    in.key_pressed[static_cast<int>(k)] = true;
    return in;
}

// Mouse at the centre of tile (tx,ty). The workspace owns the pan/zoom arithmetic
// (it centres the map on the canvas), so ask it rather than duplicating the maths
// here — a test that recomputes the layout stops testing the layout.
platform::InputState at_tile(const studioshell::MapWorkspace& ws, int tx, int ty,
                             bool left_down) {
    const ui::Rect r = ws.tile_rect(tx, ty);
    platform::InputState in{};
    in.mouse_x = r.x + r.w / 2;
    in.mouse_y = r.y + r.h / 2;
    in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = left_down;
    return in;
}

} // namespace

static void test_paint_undo_save() {
    studioshell::MapWorkspace ws(kPath);
    CHECK(ws.loaded());
    CHECK(!ws.dirty());
    CHECK(ws.map().w == 8);

    Driver d;
    d.frame(ws, platform::InputState{});          // prime the canvas rect

    // A drag across three cells of the first row.
    d.frame(ws, at_tile(ws, 1, 1, true));
    d.frame(ws, at_tile(ws, 2, 1, true));
    d.frame(ws, at_tile(ws, 3, 1, true));
    CHECK(ws.map().at("ground", 2, 1) == 1);      // painted while the button is held
    d.frame(ws, at_tile(ws, 3, 1, false));            // release commits the stroke

    CHECK(ws.dirty());
    CHECK(ws.map().at("ground", 1, 1) == 1);
    CHECK(ws.map().at("ground", 3, 1) == 1);
    CHECK(ws.map().at("ground", 4, 1) == 0);      // and nothing beyond the drag

    // The whole drag is ONE undo step, and undoing it makes the document clean again.
    ws.register_commands();
    CHECK(cmd::exists("map.undo"));
    CHECK(cmd::run("map.undo").ok);
    CHECK(ws.map().at("ground", 2, 1) == 0);
    CHECK(!ws.dirty());
    CHECK(!cmd::run("map.undo").ok);               // ...and there was only one
    CHECK(cmd::run("map.redo").ok);
    CHECK(ws.dirty());

    // Saving writes map2 and clears dirty. The file it produces must parse back.
    CHECK(cmd::run("map.save").ok);
    CHECK(!ws.dirty());
    const auto reread = tilemap::load(read_text(kPath));
    CHECK(reread.has_value());
    CHECK(reread && reread->at("ground", 2, 1) == 1);
}

static void test_autosave_and_recovery() {
    // A fresh workspace over the file the previous test saved.
    studioshell::MapWorkspace ws(kPath);
    CHECK(ws.loaded());
    CHECK(!ws.recovery_pending());

    Driver d;
    d.frame(ws, platform::InputState{});
    d.frame(ws, at_tile(ws, 5, 4, true));
    d.frame(ws, at_tile(ws, 5, 4, false));
    CHECK(ws.dirty());

    // No autosave yet — it is on a timer, not on every edit.
    CHECK(read_text(doc::autosave_path(kPath)).empty());
    d.frame(ws, platform::InputState{}, /*dt*/ 11.0);
    CHECK(!read_text(doc::autosave_path(kPath)).empty());

    // A second workspace on the same path finds the autosave and OFFERS it. The file
    // itself is untouched until the user says yes — that is the whole point.
    {
        studioshell::MapWorkspace ws2(kPath);
        CHECK(ws2.recovery_pending());
        CHECK(ws2.map().at("ground", 5, 4) == 0);     // the SAVED version
        CHECK(!ws2.dirty());

        ws2.take_recovery();
        CHECK(!ws2.recovery_pending());
        CHECK(ws2.map().at("ground", 5, 4) == 1);     // the recovered version
        CHECK(ws2.dirty());                           // ...which is not on disk yet
        // Recovery is an edit like any other, so it can be taken back.
        CHECK(ws2.map().at("ground", 2, 1) == 1);
    }

    // Declining keeps the saved file AND leaves the autosave, so a reflex click on
    // Cancel cannot be the thing that destroys the work.
    {
        studioshell::MapWorkspace ws3(kPath);
        CHECK(ws3.recovery_pending());
        ws3.dismiss_recovery();
        CHECK(!ws3.recovery_pending());
        CHECK(!read_text(doc::autosave_path(kPath)).empty());
    }
    // ...and a real save clears it, so the offer does not come back forever.
    {
        studioshell::MapWorkspace ws4(kPath);
        ws4.dismiss_recovery();
        CHECK(ws4.save().ok);
        CHECK(read_text(doc::autosave_path(kPath)).empty());
    }
}

static void test_tools_and_missing_map() {
    studioshell::MapWorkspace ws(kPath);
    Driver d;
    d.frame(ws, platform::InputState{});

    // Flood fills the connected empty region on press, once — not once per frame.
    platform::InputState g_key{};
    g_key.key_pressed[static_cast<int>(platform::Key::G)] = true;
    d.frame(ws, g_key);
    platform::InputState press = at_tile(ws, 0, 0, true);
    press.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = true;
    press.key_pressed[static_cast<int>(platform::Key::Num3)] = true;   // brush 3 first
    d.frame(ws, press);
    press.key_pressed[static_cast<int>(platform::Key::Num3)] = false;
    d.frame(ws, press);                                    // held, not pressed again
    d.frame(ws, at_tile(ws, 0, 0, false));
    CHECK(ws.map().at("ground", 0, 0) == 3);
    CHECK(ws.map().at("ground", 7, 5) == 3);               // reached the far corner
    CHECK(ws.map().at("ground", 2, 1) == 1);               // stopped at painted tiles

    ws.register_commands();
    CHECK(cmd::run("map.undo").ok);
    CHECK(ws.map().at("ground", 0, 0) == 0);
    CHECK(!cmd::run("map.undo").ok);                       // exactly one flood step

    // Right-drag erases without changing the brush.
    platform::InputState erase = at_tile(ws, 2, 1, false);
    erase.mouse_down[static_cast<int>(platform::MouseButton::Right)] = true;
    platform::InputState b_key{};
    b_key.key_pressed[static_cast<int>(platform::Key::B)] = true;
    d.frame(ws, b_key);
    d.frame(ws, erase);
    erase.mouse_down[static_cast<int>(platform::MouseButton::Right)] = false;
    d.frame(ws, erase);
    CHECK(ws.map().at("ground", 2, 1) == 0);

    // A project with no map says so instead of drawing an empty grid, and every
    // operation on it fails loudly rather than silently doing nothing.
    studioshell::MapWorkspace none("");
    CHECK(!none.loaded());
    CHECK(!none.problem().empty());
    CHECK(!none.save().ok);
    studioshell::MapWorkspace gone("maps/does_not_exist.map");
    CHECK(!gone.loaded());
    CHECK(gone.problem().find("does_not_exist") != std::string::npos);
}

// The point of the whole slice: a file this editor writes is a file the game loads.
// Until now every map2 in the repo came from a migration or a unit test — nothing an
// author had produced.
static void test_the_game_can_load_what_the_editor_writes() {
    const std::string path = "maps/authored.map";
    studioshell::MapWorkspace ws(kPath);
    Driver d;
    d.frame(ws, platform::InputState{});
    d.frame(ws, at_tile(ws, 4, 2, true));
    d.frame(ws, at_tile(ws, 4, 2, false));
    CHECK(ws.save().ok);

    // Copy what the editor produced to a second path and read it with the raycaster's
    // loader, which sniffs the magic and accepts map2 as well as fpsmap1.
    const std::string authored = read_text(kPath);
    CHECK(authored.rfind("map2", 0) == 0);       // map2, not the legacy format
    CHECK(write_text(path, authored));
    const auto game_map = fps::from_shared_text(read_text(path));
    CHECK(game_map.has_value());
    CHECK(game_map && game_map->w == 8 && game_map->h == 6);
}

// ---------------------------------------------------------------------------
//  The Entity tool — the operation that lets Map Lab die.
//
//  Driven through the CANVAS and through the PALETTE, because those are the two
//  triggers and the D-rule is that they are one operation. A tool wired to only one
//  of them is how the project ended up with a spawn editor that lived in a different
//  scene, writing a different format, for ten chapters.
// ---------------------------------------------------------------------------
static void test_entity_tool() {
    CHECK(write_text(kPath, fixture_text()));
    studioshell::MapWorkspace ws(kPath);
    CHECK(ws.loaded());
    cmd::clear();
    ws.register_commands();
    CHECK(cmd::exists("map.entity.place"));
    CHECK(cmd::exists("map.entity.facing"));

    // `spawn_player` is offered on a map that has none. Without it the tool could
    // only move entities that already exist, i.e. never make the first one.
    CHECK(ws.map().entities.empty());
    const auto names = ws.entity_names();
    CHECK(std::find(names.begin(), names.end(), std::string("spawn_player")) != names.end());
    CHECK(ws.selected_entity() == "spawn_player");

    Driver d;
    d.frame(ws, platform::InputState{});

    // A facing before there is anything to face is refused, not silently created —
    // the guard's first direction.
    const engine::OpResult early = cmd::run("map.entity.facing");
    CHECK(!early.ok);
    CHECK(early.message.find("place") != std::string::npos);
    CHECK(ws.map().entities.empty());

    // ---- through the canvas ----
    platform::InputState in = at_tile(ws, 2, 3, false);
    in.key_pressed[static_cast<int>(platform::Key::E)] = true;
    d.frame(ws, in);
    CHECK(ws.tool() == studioshell::MapWorkspace::Tool::Entity);

    d.frame(ws, at_tile(ws, 2, 3, true));          // press and drag
    CHECK(ws.map().entities.size() == 1);
    CHECK(ws.map().entity("spawn_player") != nullptr);
    if (auto* e = ws.map().entity("spawn_player")) CHECK(e->x == 2 && e->y == 3);

    d.frame(ws, at_tile(ws, 5, 3, true));          // still held: a nudge, not a new one
    d.frame(ws, at_tile(ws, 6, 3, true));
    CHECK(ws.map().entities.size() == 1);
    if (auto* e = ws.map().entity("spawn_player")) CHECK(e->x == 6);
    d.frame(ws, at_tile(ws, 6, 3, false));

    // The drag is ONE undo step; the creation that began it is another.
    CHECK(cmd::run("map.undo").ok);
    if (auto* e = ws.map().entity("spawn_player")) CHECK(e->x == 2);
    CHECK(cmd::run("map.undo").ok);
    CHECK(ws.map().entities.empty());
    CHECK(cmd::run("map.redo").ok);
    CHECK(ws.map().entities.size() == 1);

    // ---- through the palette ----
    CHECK(cmd::run("map.entity.place", {"7", "1"}).ok);
    if (auto* e = ws.map().entity("spawn_player")) CHECK(e->x == 7 && e->y == 1);
    CHECK(!cmd::run("map.entity.place", {"7"}).ok);          // too few
    CHECK(!cmd::run("map.entity.place", {"a", "b"}).ok);     // not numbers
    CHECK(!cmd::run("map.entity.place", {"99", "99"}).ok);   // off the map
    if (auto* e = ws.map().entity("spawn_player")) CHECK(e->x == 7);   // and none moved it

    // ---- facing, the guard's other direction ----
    CHECK(cmd::run("map.entity.facing").ok);
    // Stored as `dir`, in RADIANS — the property `fps::from_shared_text` reads into
    // spawn_dir, and the one the fpsmap1 migration writes. A prettier `facing E`
    // would have been a control that changed a value nothing downstream reads.
    const auto dir_of = [&ws] {
        const tilemap::Entity* e = ws.map().entity("spawn_player");
        return e ? std::strtod(tilemap::prop(e->props, "dir", "-1").c_str(), nullptr) : -1.0;
    };
    const double kHalfPi = 1.5707963267948966;
    CHECK(std::abs(dir_of() - 0.0) < 1e-9);                    // E
    CHECK(cmd::run("map.entity.facing").ok);
    CHECK(std::abs(dir_of() - kHalfPi) < 1e-6);                // S
    for (int i = 0; i < 3; ++i) CHECK(cmd::run("map.entity.facing").ok);
    CHECK(std::abs(dir_of() - 0.0) < 1e-9);                    // four steps is a full turn

    // ...and the GAME sees it. This is the assertion the first version would have
    // failed: the editor wrote `facing`, the raycaster reads `dir`, and every check
    // that stopped at "the property changed" was green.
    CHECK(cmd::run("map.entity.facing").ok);                   // -> S
    CHECK(ws.save().ok);
    const auto seen = fps::from_shared_text(read_text(kPath));
    CHECK(seen.has_value());
    if (seen) {
        CHECK(seen->spawn_cx == 7 && seen->spawn_cy == 1);
        CHECK(std::abs(seen->spawn_dir - static_cast<float>(kHalfPi)) < 1e-4f);
    }
    for (int i = 0; i < 3; ++i) CHECK(cmd::run("map.entity.facing").ok);   // back to E

    // A frame to look at: the marker on the map, its facing stub, and the ENTITY
    // section that says where the spawn is without anyone picking the tool.
    {
        // A REAL panel height. The 400x300 canvas the rest of this file uses is
        // narrower than one 8x6 map at zoom 2 and shorter than the inspector, so a
        // screenshot of it shows neither the marker nor the bottom of the panel —
        // and the first one taken proved exactly that by showing the controls
        // colliding. See test_inspector_clipped below for the other half.
        ShotDriver shot;
        shot.panel(ws);
        shot.panel(ws);
        dump_ppm(shot.buf, SW, SH, "map_entity.ppm");
        CHECK(!ws.inspector_clipped());
    }

    // ...and the guard's other direction: squeezed, it SAYS so. A control clipped
    // away is invisible and unclickable, so the status line is the only place left
    // for it to announce itself.
    {
        Driver squeezed;
        squeezed.panel(ws);
        squeezed.panel(ws);
        CHECK(ws.inspector_clipped());
        CHECK(ws.status().find("clipped") != std::string::npos);
    }

    // ---- and it survives the file ----
    CHECK(ws.save().ok);
    const auto reread = tilemap::load(read_text(kPath));
    CHECK(reread.has_value());
    if (reread) {
        const tilemap::Entity* e = reread->entity("spawn_player");
        CHECK(e != nullptr);
        if (e) {
            CHECK(e->x == 7 && e->y == 1);
            CHECK(std::abs(std::strtod(tilemap::prop(e->props, "dir", "-1").c_str(),
                                       nullptr)) < 1e-9);      // E survived the file
        }
    }
    cmd::clear();
}

// ---- chapter 134: the brush's material has a rule, and you can press it ----
static void test_rule_button() {
    CHECK(write_text(kPath, fixture_text()));
    studioshell::MapWorkspace ws(kPath);
    CHECK(ws.loaded());

    ShotDriver d;
    d.panel(ws);                                  // a draw, so rule_rect() is known
    CHECK(ws.rule_rect().h > 0);                  // ...and it fits in the panel
    CHECK(!ws.inspector_clipped());

    // At the size `--lab map` actually opens (960x600), which is smaller than the
    // 900x700 above and is the frame a person will really see. A section added to a
    // panel that was already nearly full is exactly how a control ends up drawn
    // nowhere (chapters 127, 132, 133), so the height is asserted, not assumed.
    {
        LabDriver lab;
        lab.panel(ws);
        lab.panel(ws);
        CHECK(!ws.inspector_clipped());
        CHECK(ws.rule_rect().h > 0);
    }
    // rule_rect() is where the LAST draw put it, and that draw was a different frame
    // at a different size. Redraw here or every click below lands in the lab's
    // coordinates inside the screenshot's window — which is how this test first
    // "proved" that pressing the button did nothing.
    d.panel(ws);

    // Brush 1 on `ground`, cycling none -> line -> blob -> none. Pressed the way a
    // hand presses it, not by calling cycle_rule().
    CHECK(ws.map().rule_for("ground", 1) == tilemap::RuleKind::None);
    click(d, ws, ws.rule_rect());
    CHECK(ws.map().rule_for("ground", 1) == tilemap::RuleKind::Line);
    CHECK(ws.dirty());
    click(d, ws, ws.rule_rect());
    CHECK(ws.map().rule_for("ground", 1) == tilemap::RuleKind::Blob);
    click(d, ws, ws.rule_rect());
    CHECK(ws.map().rule_for("ground", 1) == tilemap::RuleKind::None);

    // It is about the material under the BRUSH, so switching brushes switches the
    // question. Through the keyboard, which is the other way a brush is chosen.
    click(d, ws, ws.rule_rect());                 // 1 -> line
    d.panel(ws, ui::Input{}, key_press(platform::Key::Num3));
    d.panel(ws);
    CHECK(ws.map().rule_for("ground", 3) == tilemap::RuleKind::None);
    click(d, ws, ws.rule_rect());
    CHECK(ws.map().rule_for("ground", 3) == tilemap::RuleKind::Line);
    CHECK(ws.map().rule_for("ground", 1) == tilemap::RuleKind::Line);   // 1 kept its own

    // Brush 0 is EMPTY, and empty is not a material. The button is disabled — and the
    // proof that it is disabled is that pressing it says nothing at all, which is the
    // only difference between a disabled control and one whose guard refuses.
    while (ws.take_message()) {}
    d.panel(ws, ui::Input{}, key_press(platform::Key::Num0));
    d.panel(ws);
    click(d, ws, ws.rule_rect());
    CHECK(!ws.take_message().has_value());
    CHECK(ws.map().rule_for("ground", 0) == tilemap::RuleKind::None);

    // ...and the rule survives the file, which is the whole reason it lives in the map.
    CHECK(ws.save().ok);
    const auto reread = tilemap::load(read_text(kPath));
    CHECK(reread.has_value());
    if (reread) {
        CHECK(reread->rule_for("ground", 1) == tilemap::RuleKind::Line);
        CHECK(reread->rule_for("ground", 3) == tilemap::RuleKind::Line);
    }
    CHECK(read_text(kPath).rfind("map2 2\n", 0) == 0);   // it is a v2 file now

    // A frame to look at: a cross of the LINE material next to a block of the BLOB
    // one, and a third material with no rule at all. The canvas has no tileset
    // renderer, so these connectors are the only thing on screen that says the road
    // is a road — and until chapter 134 there was nothing at all.
    {
        const auto paint = [&](std::int32_t brush,
                               std::vector<std::pair<int, int>> cells) {
            d.panel(ws, ui::Input{},
                    key_press(static_cast<platform::Key>(
                        static_cast<int>(platform::Key::Num0) + brush)));
            d.panel(ws);
            for (const auto& c : cells) {
                platform::InputState press = at_tile(ws, c.first, c.second, true);
                press.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = true;
                d.panel(ws, ui::Input{}, press);
                d.panel(ws, ui::Input{}, at_tile(ws, c.first, c.second, false));
            }
        };
        paint(1, {{1, 1}, {1, 2}, {1, 3}, {0, 2}, {2, 2}, {3, 2}});
        paint(3, {{5, 1}, {6, 1}, {5, 2}, {6, 2}, {6, 3}});
        paint(5, {{0, 5}, {1, 5}});                 // no rule: no connectors at all
        CHECK(ws.map().at("ground", 1, 2) == 1);
        CHECK(ws.map().rule_for("ground", 5) == tilemap::RuleKind::None);
        // Brush 3 was given `line` above; make the block a REGION so the diagonal
        // pips have something to say — a blob is the other half of the rule.
        d.panel(ws, ui::Input{},
                key_press(static_cast<platform::Key>(static_cast<int>(platform::Key::Num0) + 3)));
        d.panel(ws);
        click(d, ws, ws.rule_rect());               // line -> blob
        CHECK(ws.map().rule_for("ground", 3) == tilemap::RuleKind::Blob);
        d.panel(ws);
        d.panel(ws);
        dump_ppm(d.buf, SW, SH, "map_rules.ppm");
    }
    cmd::clear();
}

static void test_commands_are_unregistered() {
    cmd::clear();
    {
        studioshell::MapWorkspace ws(kPath);
        ws.register_commands();
        CHECK(cmd::all().size() == 6);
        CHECK(!cmd::filter("save").empty());
    }
    // The handlers captured `this`. Leaving them behind would leave the palette
    // holding a call into freed memory — a crash that only happens when someone
    // closes a workspace and then uses the palette.
    CHECK(cmd::all().empty());
    CHECK(!cmd::exists("map.save"));
}

int main() {
    std::filesystem::remove_all(kBase);
    std::filesystem::create_directories(kBase + "/maps");
    assets::set_base_path(kBase);
    CHECK(write_text(kPath, fixture_text()));

    test_paint_undo_save();
    test_autosave_and_recovery();
    test_tools_and_missing_map();
    test_the_game_can_load_what_the_editor_writes();
    test_entity_tool();
    test_rule_button();
    test_commands_are_unregistered();

    assets::set_base_path(".");
    std::filesystem::remove_all(kBase);
    if (g_failures == 0) std::printf("map_workspace: all tests passed\n");
    else                 std::printf("map_workspace: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
