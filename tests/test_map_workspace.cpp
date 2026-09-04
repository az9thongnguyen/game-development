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
};

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

static void test_commands_are_unregistered() {
    cmd::clear();
    {
        studioshell::MapWorkspace ws(kPath);
        ws.register_commands();
        CHECK(cmd::all().size() == 4);
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
    test_commands_are_unregistered();

    assets::set_base_path(".");
    std::filesystem::remove_all(kBase);
    if (g_failures == 0) std::printf("map_workspace: all tests passed\n");
    else                 std::printf("map_workspace: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
