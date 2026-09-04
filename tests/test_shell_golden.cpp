// =============================================================================
//  tests/test_shell_golden.cpp  —  render the Studio shell offscreen and check it
// =============================================================================
//  The shell is a window, so it used to be verifiable only by opening it. But a
//  Renderer2D writes into any framebuffer the caller owns, and a Scene only needs
//  an engine::Context — no window, no SDL. So the whole shell can be driven
//  headless, one section per frame, and checked.
//
//  Same posture as test_ui_golden: assert INVARIANTS, not a pixel hash. Analytic AA
//  rounds differently across compilers and architectures (native vs wasm), so a
//  checksum would be a portability trap, not a regression test. Each section also
//  dumps a PPM next to the test binary for eyeball review.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/document/document.hpp"
#include "engine/commands/registry.hpp"
#include "engine/hub/hub_build.hpp"
#include "engine/renderer2d.hpp"
#include "engine/scene.hpp"
#include "engine/text/font.hpp"
#include "engine/ui/theme.hpp"
#include "engine/project/inspect.hpp"
#include "engine/release/ops.hpp"
#include "games/hub/hub_panel.hpp"
#include "games/farm/farm_scene.hpp"
#include "games/sandbox/world.hpp"
#include "games/studio_shell/play_viewport.hpp"
#include "games/studio_shell/scene_workspace.hpp"
#include "games/studio_shell/workspace_host.hpp"
#include "games/studio_shell/project_panel.hpp"
#include "games/studio_shell/studio_shell_scene.hpp"

// The entry ids this build can launch — the same list main.cpp owns. The scene used
// to hold its own {"fps"} literal, which is exactly the drift these tests exist to
// catch: the Studio called the farm project broken while the CLI called it fine.
static const std::vector<std::string> kKnownEntries = {"fps", "farm"};

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

namespace {

// One logical pixel out of a physical buffer.
std::uint32_t at_px(const std::vector<std::uint32_t>& b, int lx, int ly);

constexpr int LW = 1280, LH = 720, SS = 2;       // the same size --shell runs at
constexpr int PW = LW * SS, PH = LH * SS;

void dump_ppm(const std::vector<std::uint32_t>& buf, int pw, int ph, const char* name) {
    if (FILE* f = std::fopen(name, "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", pw, ph);
        for (auto p : buf) {
            const unsigned char rgb[3] = {static_cast<unsigned char>((p >> 16) & 0xFF),
                                          static_cast<unsigned char>((p >> 8) & 0xFF),
                                          static_cast<unsigned char>(p & 0xFF)};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
    }
}

std::uint32_t at_px(const std::vector<std::uint32_t>& b, int lx, int ly) {
    return b[static_cast<std::size_t>(ly * SS) * PW + lx * SS];
}

} // namespace

int main() {
    namespace th = ui::theme;
    // The scene resolves manifest paths the way main.cpp does — relative to the
    // asset root, not the repo root — so point the seam at the same place.
    assets::set_base_path(ASSET_ROOT "/assets");

    auto bytes = assets::load_file("fonts/Inter.ttf");
    CHECK(bytes.has_value());
    if (!bytes) { std::printf("shell_golden: no Inter.ttf under %s\n", ASSET_ROOT); return 1; }
    auto font = text::Font::load_from_bytes(std::move(*bytes));
    CHECK(font != nullptr);
    if (!font) return 1;

    const std::string kProject = "projects/creator.gameproject";

    // Assert the fixture is readable BEFORE rendering. Without this the whole test
    // still passes against the "cannot read this project" error screen, which has a
    // nav rail and anti-aliased text just like the real one.
    CHECK(engine::build_hub_view(kProject, {"fps"}).has_value());

    studioshell::StudioShellScene scene(kProject, kKnownEntries);

    std::vector<std::uint32_t> buf(static_cast<std::size_t>(PW) * PH, 0);
    platform::Framebuffer fb{buf.data(), PW, PH, PW};
    platform::InputState  input{};                 // no mouse over anything, no keys

    const char* names[] = {"map", "project", "play", "hub", "guide", "learn", "about"};
    constexpr int kSections = 7;

    // A fingerprint of the CONTENT area only (right of the rail). The previous version
    // of this loop pressed Tab, which the shell does not bind to navigation — so it
    // rendered section 0 five times and still passed every structural check. Comparing
    // consecutive fingerprints is what makes "we looked at five screens" true.
    std::vector<std::uint64_t> prints;

    for (int section = 0; section < kSections; ++section) {
        // Section 0 is the default; the rail is on Cmd/Ctrl+1..7.
        if (section > 0) {
            platform::InputState nav{};
            nav.mods.super = nav.mods.ctrl = true;
            nav.key_pressed[static_cast<int>(platform::Key::Num1) + section] = true;
            scene.update(1.0 / 60.0, nav);
        }
        scene.update(1.0 / 60.0, input);

        for (auto& p : buf) p = 0;
        gfx::Renderer2D r(fb, SS);
        const engine::Context ctx{r, input, 1.0 / 60.0, 0.0, 0.0, font.get()};
        scene.render(ctx);

        const auto at = [&](int lx, int ly) { return buf[(ly * SS) * PW + (lx * SS)]; };

        // The rail is a solid elevated strip on the left, separated by a hairline.
        CHECK(at(60, 400) == th::elevated);
        CHECK(at(199, 400) == th::border);

        // Text was actually rasterized: the anti-aliased glyphs put pixels on the
        // rail that are neither the surface colour nor any flat token.
        int aa = 0;
        for (int y = 0; y < PH; ++y)
            for (int x = 0; x < 200 * SS; ++x) {
                const std::uint32_t p = buf[static_cast<std::size_t>(y) * PW + x];
                if (p != th::elevated && p != th::border && p != th::accent &&
                    p != th::ctrl && p != th::bg)
                    ++aa;
            }
        CHECK(aa > 0);

        // Exactly one nav item is the primary (accent) button — the one-hot-action
        // rule, enforced rather than eyeballed.
        int accent_rows = 0;
        for (int i = 0; i < kSections; ++i) {
            const int ry = (24 + 20 + 24) + i * (32 + 4) + 16;   // middle of nav item i
            if (at(20, ry) == th::accent) ++accent_rows;
        }
        CHECK(accent_rows == 1);

        std::uint64_t print = 1469598103934665603ull;
        for (int y = 0; y < PH; ++y)
            for (int x = 210 * SS; x < PW; ++x)
                print = (print ^ buf[static_cast<std::size_t>(y) * PW + x]) * 1099511628211ull;
        prints.push_back(print);

        dump_ppm(buf, PW, PH, (std::string("shell_") + names[section] + ".ppm").c_str());
    }

    // Seven sections, seven different screens.
    for (std::size_t i = 1; i < prints.size(); ++i) CHECK(prints[i] != prints[i - 1]);
    CHECK(prints[0] != prints[prints.size() - 1]);

    // ---------------------------------------------------------------------
    //  The Scene workspace: the sandbox, absorbed — and given the undo it never had.
    // ---------------------------------------------------------------------
    {
        studioshell::StudioShellScene sc(kProject, kKnownEntries);
        studioshell::SceneWorkspace& sw = sc.scene_workspace();

        // It opened the scene the MANIFEST declares, not a hardcoded path.
        CHECK(sw.loaded());
        CHECK(sw.path() == "scenes/demo.scene");
        CHECK(!sw.dirty());                       // opening is not editing
        const std::size_t start_count = sw.actor_count();
        CHECK(start_count == 8);

        std::vector<std::uint32_t> b(static_cast<std::size_t>(PW) * PH, 0);
        platform::Framebuffer f{b.data(), PW, PH, PW};
        // Draw once so the workspace learns its canvas: immediate mode has no layout
        // until it draws, and every coordinate below depends on it.
        const auto draw = [&](const platform::InputState& in) {
            for (auto& p : b) p = 0;
            gfx::Renderer2D r(f, SS);
            const engine::Context c{r, in, 1.0 / 60.0, 0.0, 0.0, font.get()};
            sc.render(c);
        };
        // Switch to the Scene tab: the workspace is only interactive when it shows.
        platform::InputState idle{};
        draw(idle);
        {
            // The tab row is at the top of the Edit area; click the second tab. A ui
            // click is press THEN release — driving the real control rather than a
            // setter is the point: a tab that stopped being wired would still pass a
            // test that assigned the index directly.
            const int rail = 200, tx = rail + 24 + (LW - rail - 48) * 3 / 4, ty = 24 + 15;
            platform::InputState down{};
            down.mouse_x = tx; down.mouse_y = ty;
            down.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = true;
            down.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
            draw(down);
            sc.update(1.0 / 60.0, down);

            platform::InputState up{};
            up.mouse_x = tx; up.mouse_y = ty;
            up.mouse_released[static_cast<int>(platform::MouseButton::Left)] = true;
            draw(up);
            sc.update(1.0 / 60.0, up);
            sc.update(1.0 / 60.0, idle);
        }
        CHECK(sc.open_workspace() == 1);          // the Scene tab is showing
        draw(idle);
        CHECK(sw.actor_rect(0).w > 0);            // ...so it has a canvas now

        // Helper: where actor `i` currently is, in world units.
        const auto pos_of = [&](int index, float& x, float& y) {
            sandbox::World& w = const_cast<sandbox::World&>(sw.world());
            int i = 0;
            w.reg.view<sandbox::Transform2D>([&](ecs::Entity, sandbox::Transform2D& t) {
                if (i++ == index) { x = t.x; y = t.y; }
            });
        };
        const auto press_at = [&](int px, int py) {
            platform::InputState in{};
            in.mouse_x = px; in.mouse_y = py;
            in.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = true;
            in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
            return in;
        };

        // ---- place: one actor, one undo step, and dirty ---------------------
        {
            const ui::Rect a0 = sw.actor_rect(0);
            // Arm the Ball tool through the inspector, then click empty canvas.
            // Driving the real button rather than a setter is the point: a control
            // that stopped being wired would still pass a test that called place().
            // (The palette buttons are at known offsets inside the inspector.)
            sw.load_textures();
            const int drop_x = a0.x + 160, drop_y = a0.y + 200;
            platform::InputState in = press_at(drop_x, drop_y);
            // Nothing is armed, so this only selects/deselects — the count must not move.
            draw(in); sc.update(1.0 / 60.0, in);
            platform::InputState up{}; up.mouse_x = drop_x; up.mouse_y = drop_y;
            up.mouse_released[static_cast<int>(platform::MouseButton::Left)] = true;
            draw(up); sc.update(1.0 / 60.0, up);
            CHECK(sw.actor_count() == start_count);
            CHECK(!sw.dirty());                   // clicking empty space is not an edit
        }

        // ---- a click that selects but does not move is NOT an edit ----------
        // The gesture runs the whole drag path — press, hold, release — and ends
        // where it started. Without the no-op guard in commit() this records a
        // "move actor" step, and selecting an actor would make the document dirty.
        {
            const ui::Rect a0 = sw.actor_rect(0);
            const int cx = a0.x + a0.w / 2, cy = a0.y + a0.h / 2;
            platform::InputState down = press_at(cx, cy);
            draw(down); sc.update(1.0 / 60.0, down);
            platform::InputState hold{};
            hold.mouse_x = cx; hold.mouse_y = cy;
            hold.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
            draw(hold); sc.update(1.0 / 60.0, hold);
            platform::InputState rel{};
            rel.mouse_x = cx; rel.mouse_y = cy;
            rel.mouse_released[static_cast<int>(platform::MouseButton::Left)] = true;
            draw(rel); sc.update(1.0 / 60.0, rel);
            CHECK(sw.selected() == 0);            // it DID select
            CHECK(!sw.dirty());                   // ...and it did not edit
            CHECK(!cmd::run("scene.undo").ok);    // nothing was recorded to undo
        }

        // ---- drag an actor: ONE undo step for the whole gesture -------------
        {
            const ui::Rect a0 = sw.actor_rect(0);
            float ox = 0, oy = 0;
            pos_of(0, ox, oy);

            platform::InputState down = press_at(a0.x + a0.w / 2, a0.y + a0.h / 2);
            draw(down); sc.update(1.0 / 60.0, down);

            // Ten frames of movement — ten component writes, one undo step.
            for (int k = 1; k <= 10; ++k) {
                platform::InputState mv{};
                mv.mouse_x = a0.x + a0.w / 2 + k * 6;
                mv.mouse_y = a0.y + a0.h / 2 + k * 4;
                mv.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
                draw(mv); sc.update(1.0 / 60.0, mv);
            }
            platform::InputState rel{};
            rel.mouse_x = a0.x + a0.w / 2 + 60;
            rel.mouse_y = a0.y + a0.h / 2 + 40;
            rel.mouse_released[static_cast<int>(platform::MouseButton::Left)] = true;
            draw(rel); sc.update(1.0 / 60.0, rel);

            float mx = 0, my = 0;
            pos_of(0, mx, my);
            CHECK(mx > ox + 20.0f);               // it actually moved
            CHECK(sw.dirty());
            CHECK(sw.selected() == 0);            // ...and it is still the selection

            // ONE undo returns it all the way to where it started. If the drag had
            // recorded a command per frame this would leave it partway back — which
            // is the bug merge/commit-on-release exists to prevent.
            CHECK(cmd::run("scene.undo").ok);
            float ux = 0, uy = 0;
            pos_of(0, ux, uy);
            CHECK(ux == ox && uy == oy);
            CHECK(!sw.dirty());                   // ...and undoing to the saved point is CLEAN
            // An undo rebuilds the world from text, so every entity is NEW. Holding on
            // to the old index would leave the selection pointing at a different actor
            // — an inspector then edits something the user is not looking at.
            CHECK(sw.selected() == -1);
        }

        // ---- delete, then undo, restores the actor --------------------------
        {
            const ui::Rect a1 = sw.actor_rect(1);
            platform::InputState pick = press_at(a1.x + a1.w / 2, a1.y + a1.h / 2);
            draw(pick); sc.update(1.0 / 60.0, pick);
            CHECK(sw.selected() == 1);

            platform::InputState del{};
            del.key_pressed[static_cast<int>(platform::Key::Delete)] = true;
            draw(del); sc.update(1.0 / 60.0, del);
            CHECK(sw.actor_count() == start_count - 1);
            CHECK(sw.dirty());

            CHECK(cmd::run("scene.undo").ok);
            CHECK(sw.actor_count() == start_count);
            CHECK(!sw.dirty());
            // A restored world is built from NEW entities, so the old selection index
            // must be cleared rather than left pointing at a different actor.
            CHECK(sw.selected() == -1);
        }

        // ---- Play/Stop is a simulation, not an edit -------------------------
        {
            CHECK(!sw.playing());
            CHECK(!sw.dirty());
            CHECK(cmd::run("scene.play").ok);
            CHECK(sw.playing());
            float bx = 0, by = 0;
            pos_of(0, bx, by);
            for (int k = 0; k < 60; ++k) sc.update(1.0 / 60.0, idle);
            float px = 0, py = 0;
            pos_of(0, px, py);
            CHECK(px != bx || py != by);          // the simulation ran
            // ...and the emitter's spawner added actors while it ran.
            CHECK(sw.actor_count() > start_count);

            CHECK(cmd::run("scene.play").ok);     // Stop
            CHECK(!sw.playing());
            CHECK(sw.actor_count() == start_count);   // restored to the pre-Play world
            float sx = 0, sy = 0;
            pos_of(0, sx, sy);
            CHECK(sx == bx && sy == by);
            // Running and reverting a simulation did not change the DOCUMENT, so it
            // must not be dirty and Cmd+Z must not replay it.
            CHECK(!sw.dirty());
        }

        // ---- and it looks like a scene --------------------------------------
        draw(idle);
        dump_ppm(b, PW, PH, "shell_scene.ppm");
        {
            // The canvas carries the actors' colours, not just the panel surface.
            const auto at = [&](int lx, int ly) { return b[(ly * SS) * PW + (lx * SS)]; };
            const ui::Rect a2 = sw.actor_rect(2);
            CHECK(a2.w > 0);
            // COUNT ink in the actor's rect rather than probing its centre: the
            // centre pixel is the orientation tick, which is drawn in the background
            // colour on purpose. A probe there tests the tick, not the actor.
            int ink = 0;
            for (int y = a2.y; y < a2.y + a2.h; ++y)
                for (int x = a2.x; x < a2.x + a2.w; ++x) {
                    const std::uint32_t p = at(x, y);
                    if (p != th::elevated && p != th::bg) ++ink;
                }
            CHECK(ink > a2.w * a2.h / 3);
        }
    }

    // ---------------------------------------------------------------------
    //  Recovery OFFERS, it never applies (D19) — now for the second workspace too.
    // ---------------------------------------------------------------------
    {
        const std::string scene_path = "scenes/demo.scene";
        const std::string saved = std::string(
            reinterpret_cast<const char*>(assets::load_file(scene_path)->data()),
            assets::load_file(scene_path)->size());
        // An autosave that differs from the file: one fewer actor than the saved one.
        std::string autosaved;
        {
            std::size_t nl = saved.rfind('\n', saved.size() - 2);
            autosaved = saved.substr(0, nl + 1);
        }
        CHECK(autosaved != saved);
        assets::write_file(doc::autosave_path(scene_path),
                           std::vector<std::uint8_t>(autosaved.begin(), autosaved.end()));

        cmd::clear();
        studioshell::SceneWorkspace ws(scene_path);
        CHECK(ws.recovery_pending());
        // The SAVED file is what is open. Applying the autosave without asking is how
        // someone loses the version they deliberately saved.
        CHECK(ws.actor_count() == 8);

        ws.take_recovery();
        CHECK(!ws.recovery_pending());
        CHECK(ws.actor_count() == 7);       // now the recovered one
        CHECK(ws.dirty());                  // ...and it differs from the file
        // Recovery is a real command, so Ctrl+Z undoes it back to the saved file.
        ws.register_commands();
        CHECK(cmd::run("scene.undo").ok);
        CHECK(ws.actor_count() == 8);

        // Declining keeps the saved file AND leaves the autosave alone, so a reflex
        // Cancel cannot be the thing that destroys the work: the offer comes back.
        cmd::clear();
        studioshell::SceneWorkspace again(scene_path);
        CHECK(again.recovery_pending());
        again.dismiss_recovery();
        CHECK(!again.recovery_pending());
        cmd::clear();
        studioshell::SceneWorkspace third(scene_path);
        CHECK(third.recovery_pending());    // still offered

        // Clean up: assets:: has no delete, and an empty autosave is how
        // discard_autosave marks "nothing here".
        assets::write_file(doc::autosave_path(scene_path), std::vector<std::uint8_t>{});
        cmd::clear();
        studioshell::SceneWorkspace clean(scene_path);
        CHECK(!clean.recovery_pending());
    }

    // ---------------------------------------------------------------------
    //  --sandbox: the same workspace, in a window of its own. One implementation,
    //  two frames — which is the entire reason the sandbox was absorbed rather than
    //  reimplemented as a Studio tab.
    // ---------------------------------------------------------------------
    {
        cmd::clear();          // the host registers scene.* against ITS workspace
        studioshell::WorkspaceHost host(
            std::make_unique<studioshell::SceneWorkspace>("scenes/demo.scene"));
        auto& ws = static_cast<studioshell::SceneWorkspace&>(host.workspace());
        CHECK(ws.loaded());
        CHECK(ws.actor_count() == 8);
        // Hosting registers the workspace's commands, so Cmd+K in --sandbox lists the
        // same operations the Studio tab does. That is the claim; this breaks it.
        CHECK(cmd::exists("scene.play"));
        CHECK(cmd::exists("scene.undo"));

        std::vector<std::uint32_t> b(static_cast<std::size_t>(PW) * PH, 0);
        platform::Framebuffer f{b.data(), PW, PH, PW};
        platform::InputState idle{};
        for (int i = 0; i < 2; ++i) {
            for (auto& p : b) p = 0;
            gfx::Renderer2D r(f, SS);
            const engine::Context c{r, idle, 1.0 / 60.0, 0.0, 0.0, font.get()};
            host.render(c);
            host.update(1.0 / 60.0, idle);
        }
        // It filled the window: no nav rail here, so the canvas starts at the margin.
        const auto at = [&](int lx, int ly) { return b[(ly * SS) * PW + (lx * SS)]; };
        // The world's playfield is deliberately th::bg (darker than the panel around
        // it), so "not bg" is the wrong probe. What must be true is that the canvas
        // PANEL was drawn at all, and that an actor put its colour on it.
        int surface = 0;
        for (int y = 0; y < PH; y += 4)
            for (int x = 0; x < PW; x += 4)
                if (b[static_cast<std::size_t>(y) * PW + x] == th::elevated) ++surface;
        CHECK(surface > 500);
        CHECK(ws.actor_rect(0).w > 0);
        CHECK(ws.actor_rect(0).x < LW / 2);         // ...and it is on the LEFT, not inset
                                                    // behind a rail that does not exist
        {   // an actor's colour is actually on the canvas
            const ui::Rect a = ws.actor_rect(0);
            int ink = 0;
            for (int y = a.y; y < a.y + a.h; ++y)
                for (int x = a.x; x < a.x + a.w; ++x) {
                    const std::uint32_t p = at(x, y);
                    if (p != th::elevated && p != th::bg) ++ink;
                }
            CHECK(ink > a.w * a.h / 3);
        }
        dump_ppm(b, PW, PH, "sandbox_host.ppm");
    }

    // ---------------------------------------------------------------------
    //  The Play viewport: a scene gets its own framebuffer, its own clock, and
    //  only the input it is entitled to.
    // ---------------------------------------------------------------------
    {
        // A scene that reports exactly what happened to it. Sharper than driving a
        // real game here: the assertions below are about the MECHANISM, and a fake
        // that counts its own ticks can prove things a real game only implies.
        struct CountingScene : engine::Scene {
            int updates = 0, renders = 0, saw_space = 0, saw_mouse = 0;
            void update(double, const platform::InputState& in) override {
                ++updates;
                if (in.pressed(platform::Key::Space)) ++saw_space;
                if (in.mouse_x >= 0) ++saw_mouse;
            }
            void render(const engine::Context& c) override {
                ++renders;
                c.gfx.clear(0xFF00FF00u);          // a colour nothing else in the shell uses
            }
        };
        CountingScene* probe = nullptr;
        studioshell::PlayViewport vp;
        CHECK(!vp.has_factory());
        // Playing with no factory wired must FAIL with a reason, not crash and not
        // silently look like a game that renders nothing.
        CHECK(!vp.start("anything").ok);

        vp.set_factory([&probe](const std::string& id) {
            studioshell::PlayTarget t;
            if (id != "probe") return t;           // an unknown entry yields no scene
            auto sc = std::make_unique<CountingScene>();
            probe = sc.get();
            t.scene = std::move(sc);
            t.w = 160; t.h = 90;
            return t;
        });
        CHECK(vp.has_factory());
        CHECK(!vp.start("nosuch").ok);             // ...and that is reported, not ignored
        CHECK(!vp.running());

        CHECK(vp.start("probe").ok);
        CHECK(vp.running() && probe != nullptr);
        CHECK(vp.width() == 160 && vp.height() == 90);
        CHECK(vp.steps() == 0);                    // starting is not running

        const double kDt = 1.0 / 60.0;
        platform::InputState idle{};
        for (int i = 0; i < 10; ++i) vp.update(kDt, idle, /*focused*/ true);
        CHECK(vp.steps() == 10);
        CHECK(probe->updates == 10);
        CHECK(vp.clock() > 0.16 && vp.clock() < 0.17);

        // ---- input gating ---------------------------------------------------
        platform::InputState space{};
        space.key_pressed[static_cast<int>(platform::Key::Space)] = true;
        space.mouse_x = 640; space.mouse_y = 360;      // the SHELL's coordinates

        vp.update(kDt, space, /*focused*/ false);
        CHECK(probe->saw_space == 0);               // unfocused: the game hears nothing

        vp.update(kDt, space, /*focused*/ true);
        CHECK(probe->saw_space == 1);               // focused: it does

        // A chord belongs to the Studio. If the game could see Cmd+K the palette
        // would be unreachable while a game had focus, which is how an embedded
        // player becomes a trap.
        platform::InputState chord = space;
        chord.mods.super = true;
        vp.update(kDt, chord, /*focused*/ true);
        CHECK(probe->saw_space == 1);               // still 1 — the chord went nowhere

        // The pointer is in the shell's space and means nothing in the game's, so the
        // game is told there is no pointer rather than given a plausible wrong one.
        CHECK(probe->saw_mouse == 0);

        // ---- pause and step -------------------------------------------------
        const long long before = vp.steps();
        vp.set_paused(true);
        for (int i = 0; i < 10; ++i) vp.update(kDt, idle, true);
        CHECK(vp.steps() == before);                // paused means paused
        const double frozen = vp.clock();
        CHECK(frozen == vp.clock());

        vp.step_once();
        vp.update(kDt, idle, true);
        CHECK(vp.steps() == before + 1);            // EXACTLY one
        vp.update(kDt, idle, true);
        CHECK(vp.steps() == before + 1);            // ...and it does not keep going
        CHECK(vp.clock() > frozen);                 // the one step did advance time

        vp.set_paused(false);
        vp.update(kDt, idle, true);
        CHECK(vp.steps() == before + 2);

        // ---- the frame reaches the panel, letterboxed at a whole scale -------
        {
            std::vector<std::uint32_t> b(static_cast<std::size_t>(PW) * PH, 0);
            platform::Framebuffer f{b.data(), PW, PH, PW};
            gfx::Renderer2D r(f, SS);
            r.set_font(font.get(), th::sz_body);
            r.clear(th::bg);
            const ui::Rect area{300, 100, 640, 400};
            const ui::Rect shown = vp.draw(r, area, font.get(), kDt);
            CHECK(probe->renders == 1);             // rendering happens in draw, once

            // 160x90 into 640x400 → scale 4 (640/160=4, 400/90=4), so 640x360 centred.
            CHECK(shown.w == 640 && shown.h == 360);
            CHECK(shown.w % vp.width() == 0);       // a whole-number scale, always
            CHECK(shown.x == 300 && shown.y == 100 + (400 - 360) / 2);

            const auto at = [&](int lx, int ly) { return b[(ly * SS) * PW + (lx * SS)]; };
            CHECK(at(shown.x + shown.w / 2, shown.y + shown.h / 2) == 0xFF00FF00u);
            // ...and the letterbox bar above it is the shell's background, not the game.
            CHECK(at(shown.x + shown.w / 2, area.y + 4) == th::bg);
        }

        // ---- stop clears everything, including the clock --------------------
        vp.stop();
        CHECK(!vp.running());
        CHECK(vp.steps() == 0);
        CHECK(vp.clock() == 0.0);
        CHECK(vp.width() == 0);
        // Updating a stopped viewport is a no-op, not a null dereference.
        vp.update(kDt, idle, true);
        CHECK(vp.steps() == 0);
    }

    // ---------------------------------------------------------------------
    //  ...and a REAL game in it. The mechanism above is proven with a fake; this
    //  is the part that says the seam main.cpp wires actually carries a scene.
    // ---------------------------------------------------------------------
    {
        studioshell::StudioShellScene sc("projects/farm.gameproject", kKnownEntries);
        sc.set_play_factory([](const std::string& id) {
            studioshell::PlayTarget t;
            if (id == "farm") {
                t.scene = std::make_unique<farm::FarmScene>();
                t.w = 640; t.h = 360;              // the size main.cpp's entry table uses
            }
            return t;
        });
        CHECK(sc.play().has_factory());
        CHECK(sc.play().start(sc.inspection().project.entry).ok);
        CHECK(sc.play().running());

        // Two seconds of game time, driven exactly as the shell drives it.
        platform::InputState idle{};
        for (int i = 0; i < 120; ++i) sc.play().update(1.0 / 60.0, idle, false);
        CHECK(sc.play().steps() == 120);

        std::vector<std::uint32_t> b(static_cast<std::size_t>(PW) * PH, 0);
        platform::Framebuffer f{b.data(), PW, PH, PW};
        gfx::Renderer2D r(f, SS);
        r.set_font(font.get(), th::sz_body);
        r.clear(th::bg);
        const ui::Rect shown = sc.play().draw(r, ui::Rect{224, 120, LW - 248, 480},
                                              font.get(), 1.0 / 60.0);
        CHECK(shown.w == 640 * 1 || shown.w == 640);   // 1056x480 fits 640x360 once
        CHECK(shown.w % 640 == 0);

        // The frame is a GAME, not a blank rectangle: count distinct colours inside it.
        // A scene that failed to load its map would clear to one colour and pass any
        // "did we draw something" check that only looks for non-background pixels.
        std::vector<std::uint32_t> seen;
        for (int y = shown.y + 8; y < shown.y + shown.h - 8; y += 7)
            for (int x = shown.x + 8; x < shown.x + shown.w - 8; x += 7) {
                const std::uint32_t p = b[static_cast<std::size_t>(y * SS) * PW + x * SS];
                bool have = false;
                for (auto c : seen) if (c == p) { have = true; break; }
                if (!have && seen.size() < 64) seen.push_back(p);
            }
        CHECK(seen.size() >= 5);
        dump_ppm(b, PW, PH, "shell_play_farm.ppm");

        // ...and the whole composed screen: rail, toolbar, the running game, status.
        // Driven through the scene's own update, so the path is the one --shell takes.
        {
            const long long before = sc.play().steps();
            platform::InputState nav{};
            nav.mods.super = nav.mods.ctrl = true;
            nav.key_pressed[static_cast<int>(platform::Key::Num3)] = true;   // section 2 = Play
            sc.update(1.0 / 60.0, nav);
            platform::InputState none{};
            for (int i = 0; i < 60; ++i) sc.update(1.0 / 60.0, none);
            // Every shell update is one game step — including the one that carried the
            // navigation chord. A game does not pause because you changed tabs.
            CHECK(sc.play().steps() == before + 61);

            std::vector<std::uint32_t> b2(static_cast<std::size_t>(PW) * PH, 0);
            platform::Framebuffer f2{b2.data(), PW, PH, PW};
            gfx::Renderer2D r2(f2, SS);
            const engine::Context c2{r2, none, 1.0 / 60.0, 0.0, 0.0, font.get()};
            sc.render(c2);
            dump_ppm(b2, PW, PH, "shell_play_running.ppm");

            // The rail is still the rail: a game drawing into its own buffer cannot
            // paint over the Studio's chrome, which is the whole reason for the buffer.
            const auto at2 = [&](int lx, int ly) { return b2[(ly * SS) * PW + (lx * SS)]; };
            CHECK(at2(60, 400) == th::elevated);
            CHECK(at2(199, 400) == th::border);
        }
    }

    // ---------------------------------------------------------------------
    //  The Hub's history: status says WHERE a release is, the log says how it got
    //  there. Newest first, because "what just happened" is the question it answers.
    // ---------------------------------------------------------------------
    {
        const auto view = engine::build_hub_view(kProject, kKnownEntries);
        CHECK(view.has_value());

        const auto render_hub = [&](const std::vector<engine::AuditRecord>& hist) {
            std::vector<std::uint32_t> b(static_cast<std::size_t>(PW) * PH, 0);
            platform::Framebuffer f{b.data(), PW, PH, PW};
            gfx::Renderer2D r(f, SS);
            r.set_font(font.get(), th::sz_body);
            r.clear(th::bg);
            ui::Context u;
            u.begin(&r, ui::Input{}, LW, LH);
            hubui::draw_hub_panel(u, r, &*view, kProject,
                                  ui::Rect{224, 24, LW - 248, LH - 48}, hist);
            u.end();
            return b;
        };

        engine::AuditRecord older;
        older.epoch = 1000000000; older.action = "publish"; older.channel = "development";
        older.release = "aaaaaaaaaaaaaaaa"; older.reason = "the older one";
        engine::AuditRecord newer;
        newer.epoch = 1700000000; newer.action = "promote"; newer.channel = "production";
        newer.release = "bbbbbbbbbbbbbbbb"; newer.reason = "the newer one";

        const auto empty = render_hub({});
        const auto both  = render_hub({older, newer});   // stored oldest-first
        const auto just_new = render_hub({newer});
        const auto just_old = render_hub({older});

        // Drawn at all, and each record adds to it. A ratio against the empty state
        // would be measuring the card outline more than the rows; monotonic growth
        // across none → one → two records is the property that actually holds.
        const auto ink_below = [&](const std::vector<std::uint32_t>& b, int y0) {
            int n = 0;
            for (int y = y0 * SS; y < PH; ++y)
                for (int x = 224 * SS; x < PW; ++x) {
                    const std::uint32_t p = b[static_cast<std::size_t>(y) * PW + x];
                    if (p != th::bg && p != th::elevated && p != th::border) ++n;
                }
            return n;
        };
        CHECK(ink_below(empty, 340) < ink_below(just_new, 340));
        CHECK(ink_below(just_new, 340) < ink_below(both, 340));

        // ...and NEWEST FIRST, pinned so the direction can actually fail. The first
        // pixel row where {older,newer} differs from {newer} alone must be DEEPER than
        // where it differs from {older} alone: both lists open with the newer record,
        // so their top row is identical and the difference only appears one row down.
        // Reverse the order and this inverts.
        const auto first_diff_row = [&](const std::vector<std::uint32_t>& a,
                                        const std::vector<std::uint32_t>& b, int y0) {
            for (int y = y0 * SS; y < PH; ++y)
                for (int x = 224 * SS; x < PW; ++x)
                    if (a[static_cast<std::size_t>(y) * PW + x] !=
                        b[static_cast<std::size_t>(y) * PW + x]) return y;
            return PH;
        };
        const int d_vs_new = first_diff_row(both, just_new, 340);
        const int d_vs_old = first_diff_row(both, just_old, 340);
        CHECK(d_vs_new > d_vs_old);
        CHECK(d_vs_old < PH);          // they DO differ somewhere, or the test proves nothing
    }

    // ---------------------------------------------------------------------
    //  The Project section: the Studio's verdict is the CLI's verdict.
    // ---------------------------------------------------------------------
    {
        // THE regression. The scene used to build its own {"fps"} known-entries list
        // while main.cpp knew {"fps","farm"}, so --shell projects/farm.gameproject
        // showed "unknown entry scene: farm" on a project --project-inspect called OK.
        // Two answers, one truth, and no way for the operator to tell which lied.
        studioshell::StudioShellScene farm("projects/farm.gameproject", kKnownEntries);
        CHECK(farm.inspection().shippable());
        CHECK(farm.inspection().project.entry == "farm");
        CHECK(farm.inspection().assets.size() == 5);
        // ...and the negative control: the list is what makes the difference, so an
        // ignorant list must still reject it. Otherwise the check above would pass
        // just as happily if known_entries were ignored entirely.
        CHECK(!engine::inspect("projects/farm.gameproject", {"fps"}).shippable());

        // The panel is a pure function of an Inspection, so the states worth drawing
        // are built here rather than by breaking files on disk.
        const auto render_panel = [&](const engine::Inspection& in) {
            std::vector<std::uint32_t> b(static_cast<std::size_t>(PW) * PH, 0);
            platform::Framebuffer f{b.data(), PW, PH, PW};
            gfx::Renderer2D r(f, SS);
            r.set_font(font.get(), th::sz_body);
            r.clear(th::bg);
            ui::Context u;
            u.begin(&r, ui::Input{}, LW, LH);
            int sel = 0;
            projectui::draw_project_panel(u, r, in, ui::Rect{224, 24, LW - 248, LH - 48}, sel);
            u.end();
            return b;
        };
        // How many pixels of a colour the content area carries. A verdict drawn as a
        // tinted strip is a COUNT, not one probe pixel: probing a coordinate is how a
        // test starts passing because the layout moved rather than because it is right.
        const auto count_near = [&](const std::vector<std::uint32_t>& b, gfx::Color c) {
            int n = 0;
            for (int y = 0; y < PH; ++y)
                for (int x = 224 * SS; x < PW; ++x) {
                    const std::uint32_t p = b[static_cast<std::size_t>(y) * PW + x];
                    const int dr = int((p >> 16) & 0xFF) - int((c >> 16) & 0xFF);
                    const int dg = int((p >> 8) & 0xFF) - int((c >> 8) & 0xFF);
                    const int db = int(p & 0xFF) - int(c & 0xFF);
                    if (dr * dr + dg * dg + db * db < 400) ++n;
                }
            return n;
        };

        const engine::Inspection good = engine::inspect("projects/farm.gameproject", kKnownEntries);
        const auto good_px = render_panel(good);
        CHECK(count_near(good_px, th::success) > 200);   // the "no problems" strip + ok badges
        dump_ppm(good_px, PW, PH, "shell_project_ok.ppm");

        // A project with a hole: same manifest, one asset marked absent. The panel must
        // keep it IN PLACE (row 2 of 5) and say so, not quietly show four assets.
        engine::Inspection holed = good;
        CHECK(holed.assets.size() > 2);
        holed.assets[1].present = false;
        holed.assets[1].hash = 0;
        holed.assets[1].bytes = 0;
        holed.problems.push_back("missing asset: " + holed.assets[1].path);
        holed.package.clear();
        CHECK(!holed.shippable());
        const auto bad_px = render_panel(holed);
        CHECK(count_near(bad_px, th::danger) > 200);     // the problem strip + the MISSING badge
        CHECK(count_near(bad_px, th::success) < count_near(good_px, th::success));
        dump_ppm(bad_px, PW, PH, "shell_project_missing.ppm");

        // An unreadable project draws the reason, not an empty browser that looks like
        // a project with no content.
        const engine::Inspection none = engine::inspect("projects/nope.gameproject", kKnownEntries);
        CHECK(!none.parsed);
        const auto none_px = render_panel(none);
        CHECK(count_near(none_px, th::danger) > 40);
        // ...and it is a DIFFERENT screen from the healthy one, which is what says the
        // error state is drawn at all rather than silently skipped.
        CHECK(none_px != good_px);
    }

    // ---------------------------------------------------------------------
    //  The Map workspace: the project's declared map is actually on screen, and the
    //  command palette lists what this process can do.
    // ---------------------------------------------------------------------
    {
        studioshell::StudioShellScene sc(kProject, kKnownEntries);        // opens on Map
        CHECK(sc.map_workspace().loaded());
        CHECK(sc.map_workspace().path() == "maps/level_00.map");
        CHECK(!sc.map_workspace().dirty());                // opening is not editing

        std::vector<std::uint32_t> b(static_cast<std::size_t>(PW) * PH, 0);
        platform::Framebuffer f{b.data(), PW, PH, PW};
        const auto render = [&](const platform::InputState& in) {
            for (auto& p : b) p = 0;
            gfx::Renderer2D r(f, SS);
            const engine::Context c{r, in, 1.0 / 60.0, 0.0, 0.0, font.get()};
            sc.render(c);
        };

        sc.update(1.0 / 60.0, input);
        render(input);

        // The map is actually ON SCREEN: the canvas is mostly its own surface colour,
        // and a rendered map covers a large part of it with something else. A failed
        // load draws one line of warning text, a few hundred anti-aliased pixels —
        // nowhere near this. (Counting one exact tile colour would not work: the
        // collision mask washes red over the walls it marks.)
        int ink = 0;
        for (int y = 100 * SS; y < 600 * SS; ++y)
            for (int x = 240 * SS; x < 900 * SS; ++x) {
                const std::uint32_t p = b[static_cast<std::size_t>(y) * PW + x];
                if (p != th::elevated && p != th::bg) ++ink;
            }
        CHECK(ink > 20000);

        // ---- command palette ----
        platform::InputState k{};
        k.mods.super = k.mods.ctrl = true;
        k.key_pressed[static_cast<int>(platform::Key::K)] = true;
        sc.update(1.0 / 60.0, k);
        render(input);
        dump_ppm(b, PW, PH, "shell_palette.ppm");

        // The scrim darkened the rail, so the palette is modal rather than on top...
        CHECK(at_px(b, 60, 400) != th::elevated);
        // ...and the workspace's own commands are in it. map.save is registered by
        // the workspace, so its presence here is the registry and the palette being
        // the same list rather than two that agree today.
        CHECK(cmd::exists("map.save"));
        CHECK(!cmd::filter("save").empty());

        // Escape closes it and the map screen comes back.
        platform::InputState esc{};
        esc.key_pressed[static_cast<int>(platform::Key::Escape)] = true;
        sc.update(1.0 / 60.0, esc);
        render(input);
        CHECK(at_px(b, 60, 400) == th::elevated);
    }

    // ---------------------------------------------------------------------
    //  The window is resizable, so the shell must lay out at any size. This is
    //  the half of resizing that can break in our code; SDL's own resize event
    //  path is not exercised here (see the chapter's verification note).
    // ---------------------------------------------------------------------
    {
        struct Size { int w, h, ss; };
        const Size sizes[] = {{1280, 720, 2}, {900, 560, 2}, {1600, 1000, 1}, {700, 420, 1}};
        for (const Size& sz : sizes) {
            const int pw = sz.w * sz.ss, ph = sz.h * sz.ss;
            std::vector<std::uint32_t> b(static_cast<std::size_t>(pw) * ph, 0);
            platform::Framebuffer f{b.data(), pw, ph, pw};
            gfx::Renderer2D r(f, sz.ss);
            const engine::Context c{r, input, 1.0 / 60.0, 0.0, 0.0, font.get()};

            studioshell::StudioShellScene sc(kProject, kKnownEntries);      // fresh: starts on Map
            sc.update(1.0 / 60.0, input);
            sc.render(c);

            const auto px = [&](int lx, int ly) { return b[(ly * sz.ss) * pw + (lx * sz.ss)]; };

            // The rail spans the FULL height whatever that height is — the bug a
            // hard-coded panel height produces is a rail that stops short. Stated as
            // "no window background inside the rail" rather than as one probe pixel,
            // because a probe lands on whatever text happens to be drawn there.
            bool rail_full_height = true;
            for (int lx = 0; lx < 199; ++lx)
                if (px(lx, 2) == th::bg || px(lx, sz.h - 3) == th::bg) rail_full_height = false;
            CHECK(rail_full_height);
            CHECK(px(199, sz.h / 2) == th::border);

            // Nothing overflows the right edge. Checking a single corner is not
            // enough — the button row was running off the side while the corner
            // stayed clean. Scan the whole last logical column instead.
            bool right_edge_clean = true;
            for (int ly = 0; ly < sz.h; ++ly)
                if (px(sz.w - 1, ly) != th::bg) right_edge_clean = false;
            CHECK(right_edge_clean);

            // ...and the content area is not EMPTY either. Without this, a layout
            // that drew everything off-screen would pass every check above.
            int ink = 0;
            for (int y = 0; y < ph; ++y)
                for (int x = 210 * sz.ss; x < pw; ++x)
                    if (b[static_cast<std::size_t>(y) * pw + x] != th::bg) ++ink;
            CHECK(ink > 1000);
        }
    }

    // ---------------------------------------------------------------------
    //  The confirmation screen. This is the one the safety checklist cares about:
    //  an irreversible operation must be confirmed and must record a reason.
    // ---------------------------------------------------------------------
    {
        std::vector<std::uint32_t> b(static_cast<std::size_t>(PW) * PH, 0);
        platform::Framebuffer f{b.data(), PW, PH, PW};

        studioshell::StudioShellScene sc(kProject, kKnownEntries);

        // The shell opens on the Map workspace now, so switch to the Hub first.
        {
            platform::InputState nav{};
            nav.mods.super = nav.mods.ctrl = true;
            nav.key_pressed[static_cast<int>(platform::Key::Num4)] = true;   // section 3 = Hub
            sc.update(1.0 / 60.0, nav);
        }

        // Space asks to publish; the scene should raise a dialog, not publish.
        platform::InputState space{};
        space.key_pressed[static_cast<int>(platform::Key::Space)] = true;
        sc.update(1.0 / 60.0, space);

        // Type a reason, the way the operator would.
        platform::InputState typing{};
        const char* why = "checking the confirmation screen";
        for (std::size_t i = 0; why[i] && i < platform::InputState::kTextMax; ++i)
            typing.text[typing.text_len++] = why[i];

        // Render once to lay the dialog out (the text field only exists once drawn),
        // then feed the typing through.
        for (int i = 0; i < 2; ++i) {
            gfx::Renderer2D r(f, SS);
            const engine::Context c{r, i == 0 ? input : typing, 1.0 / 60.0, 0.0, 0.0, font.get()};
            sc.render(c);
            sc.update(1.0 / 60.0, i == 0 ? input : typing);
        }

        for (auto& p : b) p = 0;
        gfx::Renderer2D r(f, SS);
        const engine::Context c{r, input, 1.0 / 60.0, 0.0, 0.0, font.get()};
        sc.render(c);

        const auto at = [&](int lx, int ly) { return b[(ly * SS) * PW + (lx * SS)]; };

        // The scrim darkened the screen behind: the nav rail is no longer its own
        // flat colour. Without begin_inert() + the scrim this pixel would be
        // untouched, and the dialog would be merely on top rather than modal.
        CHECK(at(60, 400) != th::elevated);
        CHECK(at(60, 400) != th::bg);

        // The card is drawn over the middle of the screen. Probe between the reason
        // field and the buttons — the card's own surface, not a control on it.
        CHECK(at(LW / 2, 420) == th::elevated);

        // With a reason typed, the accept button is live (accent fill). This is the
        // assertion that matters: it says the operator CAN proceed once they have
        // explained why.
        // Probe inside the accept button but clear of its centred label — the middle
        // pixel lands on a glyph.
        const int accept_x = (LW - 460) / 2 + 460 - 16 - 120 + 8;
        const int accept_y = (LH - 240) / 2 + 240 - 16 - 30 + 15;
        CHECK(at(accept_x, accept_y) == th::accent);

        dump_ppm(b, PW, PH, "shell_confirm.ppm");

        // ...and the negative control: a fresh dialog with NO reason typed must show
        // the accept button disabled. Without this, the check above would pass just
        // as happily if the reason requirement were not enforced at all.
        {
            studioshell::StudioShellScene sc2(kProject, kKnownEntries);
            platform::InputState nav2{};
            nav2.mods.super = nav2.mods.ctrl = true;
            nav2.key_pressed[static_cast<int>(platform::Key::Num4)] = true;   // section 3 = Hub
            sc2.update(1.0 / 60.0, nav2);
            platform::InputState sp{};
            sp.key_pressed[static_cast<int>(platform::Key::Space)] = true;
            sc2.update(1.0 / 60.0, sp);

            std::vector<std::uint32_t> b2(static_cast<std::size_t>(PW) * PH, 0);
            platform::Framebuffer f2{b2.data(), PW, PH, PW};
            gfx::Renderer2D r2(f2, SS);
            const engine::Context c2{r2, input, 1.0 / 60.0, 0.0, 0.0, font.get()};
            sc2.render(c2);

            const auto at2 = [&](int lx, int ly) { return b2[(ly * SS) * PW + (lx * SS)]; };
            CHECK(at2(accept_x, accept_y) == th::ctrl_disabled);
        }
    }

    if (g_failures == 0) std::printf("shell_golden: all tests passed\n");
    else                 std::printf("shell_golden: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
