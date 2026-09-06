// =============================================================================
//  tests/test_mix_workspace.cpp  —  the Studio's parts mixer, driven headless
// =============================================================================
//  The workspace opens the project's real `.mix` sources and its real parts sheet,
//  because the thing under test is the LOOP: click a part, see the sprite change,
//  save the source, bake the artefact, and get the same bytes the CLI would write.
//  A fixture made of invented pixels would test `mix::compose`, which already has
//  its own file.
//
//  It works on COPIES in a scratch asset root — the committed `.mix` files must not
//  move because a test ran.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/commands/asset_commands.hpp"
#include "engine/commands/registry.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/ui.hpp"
#include "games/studio_shell/mix_workspace.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace fs = std::filesystem;

namespace {

constexpr int PW = 720, PH = 560;
const fs::path kTmp = fs::temp_directory_path() / "mix_ws_tmp";

struct Driver {
    std::vector<std::uint32_t> buf =
        std::vector<std::uint32_t>(static_cast<std::size_t>(PW) * PH, 0);
    platform::Framebuffer fb{buf.data(), PW, PH, PW};
    ui::Context           ui;

    void panel(studioshell::MixWorkspace& ws, const ui::Input& uin = {},
               const platform::InputState& in = platform::InputState{}) {
        const int       iw = ws.inspector_width();
        gfx::Renderer2D g(fb, 1);
        ui.begin(&g, uin, PW, PH);
        ws.draw_canvas(ui, g, ui::Rect{0, 0, PW - iw, PH});
        ws.draw_inspector(ui, g, ui::Rect{PW - iw, 0, iw, PH});
        ui.end();
        ws.update(1.0 / 60.0, in, /*interactive*/ true);
    }
};

// ui::interact fires on RELEASE over the rect. A test that only presses proves a
// widget was drawn and never that it can be pressed.
void click(Driver& d, studioshell::MixWorkspace& ws, ui::Rect r) {
    if (r.w <= 0 || r.h <= 0) return;
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    ui::Input p{}; p.mx = cx; p.my = cy; p.down = true; p.pressed = true;
    ui::Input u{}; u.mx = cx; u.my = cy; u.released = true;
    d.panel(ws, p);
    d.panel(ws, u);
    d.panel(ws);
}

// The CANVAS reads platform::InputState, not ui::Input — two input paths through one
// frame, and feeding only the widget one would click nothing while looking like it had.
void click_canvas(Driver& d, studioshell::MixWorkspace& ws, ui::Rect r) {
    if (r.w <= 0) return;
    platform::InputState in{};
    in.mouse_x = r.x + r.w / 2;
    in.mouse_y = r.y + r.h / 2;
    in.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = true;
    in.mouse_down[static_cast<int>(platform::MouseButton::Left)]    = true;
    d.panel(ws, ui::Input{}, in);
    d.panel(ws);
}

// Click a named inspector control, and say whether it was reachable at all. A helper
// that silently did nothing for an unreachable control would pass every assertion
// after it while the panel was unusable (chapter 133).
bool click_and_check(Driver& d, studioshell::MixWorkspace& ws, const char* id) {
    const ui::Rect r = ws.control_rect(id);
    if (r.w <= 0 || r.h <= 0) return false;
    click(d, ws, r);
    return true;
}

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

std::vector<std::uint8_t> read_bytes(const std::string& p) {
    auto b = assets::load_file(p);
    return b ? *b : std::vector<std::uint8_t>{};
}

std::vector<std::string> project_textures() {
    return {"textures/farm_player.hrt", "textures/farm_anna.hrt", "textures/town.hrt"};
}

} // namespace

// ---- 1. it opens exactly the assets that HAVE a source ---------------------
static void test_opens_the_mixed_ones_only() {
    studioshell::MixWorkspace ws(project_textures());
    CHECK(ws.loaded());
    // town.hrt is an IMPORT: it has no `.mix` beside it, so the Mixer must not offer
    // it. The openable set is derived from the same sibling rule provenance_core uses,
    // rather than from a second list that could disagree with the ledger.
    CHECK(ws.mixes().size() == 2);
    for (const std::string& p : ws.mixes()) CHECK(p.find("town") == std::string::npos);
    CHECK(ws.part_count() >= 2);
    CHECK(ws.preview().w == 16 && ws.preview().h == 16);
    CHECK(ws.sheet_tiles() == 3);          // body, head, hat

    // A project with no `.mix` at all is a normal state with a reason, not a crash.
    studioshell::MixWorkspace empty(std::vector<std::string>{"textures/town.hrt"});
    CHECK(!empty.loaded());
    CHECK(!empty.problem().empty());
}

// ---- 2. the loop: add a part, see it, undo it ------------------------------
static void test_add_and_remove_a_part() {
    Driver d;
    studioshell::MixWorkspace ws(project_textures());
    ws.register_commands();
    d.panel(ws);
    const std::size_t before = ws.part_count();
    const gfx::Image  was    = ws.preview();

    // The hat, clicked in the strip under the sprite — the way a hand adds one.
    const ui::Rect hat = ws.part_rect(2);
    CHECK(hat.w > 0);
    click_canvas(d, ws, hat);
    CHECK(ws.part_count() == before + 1);
    CHECK(ws.dirty());
    // The PICTURE changed, which is the claim. A part that appended to the document
    // and drew nothing would pass every count above.
    CHECK(ws.preview().pixels != was.pixels);

    CHECK(cmd::run("mix.undo").ok);
    CHECK(ws.part_count() == before);
    CHECK(ws.preview().pixels == was.pixels);

    // Remove refuses to leave the mix empty: a mix that composes nothing is not a
    // document, and the button says so rather than going quiet.
    while (ws.part_count() > 1) CHECK(ws.remove_top().ok);
    const engine::OpResult refused = ws.remove_top();
    CHECK(!refused.ok);
    CHECK(refused.message.find("at least one") != std::string::npos);
}

// ---- 3. clicking the sprite recolours it -----------------------------------
static void test_click_to_recolour() {
    Driver d;
    studioshell::MixWorkspace ws(project_textures());
    ws.register_commands();
    d.panel(ws);
    const gfx::Image was = ws.preview();

    // Find a solid pixel of the tunic to aim at, in preview coordinates. Row 10 is
    // inside the body; the sprite is 16x16 and its middle column is opaque there.
    const ui::Rect px = ws.preview_pixel_rect(8, 10);
    CHECK(px.w > 0);
    click_canvas(d, ws, px);
    CHECK(ws.preview().pixels != was.pixels);
    CHECK(ws.dirty());
    CHECK(!ws.doc().swaps.empty());

    // Transparent is refused with a reason: there is no colour there to change, and a
    // silent no-op would read as a broken button.
    const engine::OpResult nothing = ws.cycle_swap(0);
    CHECK(!nothing.ok);
    CHECK(nothing.message.find("nothing there") != std::string::npos);

    CHECK(cmd::run("mix.undo").ok);
    CHECK(ws.preview().pixels == was.pixels);
}

// ---- 4. Save writes the SOURCE, Bake writes the ARTEFACT -------------------
// Two buttons because they are two things, and this is the assertion that says so:
// saving must not touch the `.hrt`, and baking must produce exactly what the CLI
// would — the button and the terminal go through one command (D-rule).
static void test_save_is_not_bake() {
    Driver d;
    studioshell::MixWorkspace ws(project_textures());
    d.panel(ws);
    const std::string mix_path = ws.path();
    const auto        hrt_path = mix_path.substr(0, mix_path.rfind('.')) + ".hrt";

    const auto hrt_before = read_bytes(hrt_path);
    const auto mix_before = read_bytes(mix_path);
    CHECK(!hrt_before.empty() && !mix_before.empty());

    click_canvas(d, ws, ws.part_rect(2));            // add the hat
    CHECK(ws.dirty());

    CHECK(click_and_check(d, ws, "save"));
    CHECK(!ws.dirty());
    CHECK(read_bytes(mix_path) != mix_before);       // the source moved...
    CHECK(read_bytes(hrt_path) == hrt_before);       // ...and the artefact did NOT

    CHECK(click_and_check(d, ws, "bake"));
    const auto hrt_after = read_bytes(hrt_path);
    CHECK(hrt_after != hrt_before);

    // ...and it is byte-for-byte what the command line would have written.
    CHECK(cmd::run("asset.mix", {mix_path, "textures/_cli.hrt"}).ok);
    CHECK(read_bytes("textures/_cli.hrt") == hrt_after);
}

// ---- 5. a frame for a human to look at -------------------------------------
static void test_screenshot() {
    Driver d;
    studioshell::MixWorkspace ws(project_textures());
    d.panel(ws);
    click_canvas(d, ws, ws.part_rect(2));
    click_canvas(d, ws, ws.preview_pixel_rect(8, 10));
    d.panel(ws);
    dump_ppm(d.buf, PW, PH, "mix_workspace.ppm");
}

int main() {
    std::error_code ec;
    fs::remove_all(kTmp, ec);
    fs::create_directories(kTmp, ec);
    for (const char* dir : {"textures"})
        fs::copy(fs::path(ASSET_ROOT) / "assets" / dir, kTmp / dir,
                 fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    assets::set_base_path(kTmp.string());
    cmd::clear();
    cmd::register_asset_commands();

    test_opens_the_mixed_ones_only();
    test_add_and_remove_a_part();
    test_click_to_recolour();
    test_save_is_not_bake();
    test_screenshot();

    cmd::clear();
    assets::set_base_path(".");
    fs::remove_all(kTmp, ec);
    if (g_failures == 0) std::printf("mix_workspace: all tests passed\n");
    else                 std::printf("mix_workspace: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
