// =============================================================================
//  tests/test_scene_workspace.cpp  —  the Studio's scene editor, driven headless
// =============================================================================
//  The scene workspace was the only one of the three with no test at all. It got one
//  the chapter it grew the effect components (133), because that is the chapter it
//  grew an inspector taller than its own panel — and "a control that is drawn but
//  cannot be pressed" has now been the bug in three consecutive chapters (127, 132,
//  and the scroll viewport here).
//
//  So the questions this file asks are deliberately about REACH, not just about
//  state: is the control inside the viewport, does a click on it change the document,
//  and — the direction nobody checks — does a click at the coordinates of a control
//  that has been scrolled away do nothing at all.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"
#include "engine/ui/ui.hpp"
#include "games/studio_shell/sound_bank.hpp"
#include "games/sandbox/serialize.hpp"
#include "games/studio_shell/scene_workspace.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

const std::string kBase = "test_scene_ws_tmp";
const std::string kPath = "scenes/t.scene";

// One animated actor, so the FLIPBOOK section has something to show, plus a coin and
// something that eats it — the only event a Sound is heard on.
const char* const kFixture =
    "sandbox1\n"
    "bounds 320 200\n"
    "e x=60 y=60 rot=0 color=f0c846 w=24 h=24 frames=4 fps=8\n"
    "e x=200 y=120 rot=0 color=f0dc78 round w=16 h=16 tag=1 sound=523,120,0.5\n"
    "e x=200 y=120 rot=0 color=e65a50 w=20 h=20 onoverlap=1:other\n";

bool write_text(const std::string& path, const std::string& text) {
    return assets::write_file(path, std::vector<std::uint8_t>(text.begin(), text.end()));
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

// Deliberately short: the panel must be too small for the body once three effects are
// on, because that is the situation the scroll viewport exists for.
constexpr int PW = 720, PH = 480;

struct Driver {
    std::vector<std::uint32_t> buf =
        std::vector<std::uint32_t>(static_cast<std::size_t>(PW) * PH, 0);
    platform::Framebuffer fb{buf.data(), PW, PH, PW};
    ui::Context           ui;

    void panel(studioshell::SceneWorkspace& ws, const ui::Input& uin,
               const platform::InputState& in = platform::InputState{},
               double dt = 1.0 / 60.0) {
        const int       iw = ws.inspector_width();
        gfx::Renderer2D g(fb, 1);
        ui.begin(&g, uin, PW, PH);
        ws.draw_canvas(ui, g, ui::Rect{0, 0, PW - iw, PH});
        ws.draw_inspector(ui, g, ui::Rect{PW - iw, 0, iw, PH});
        ui.end();
        ws.update(dt, in, /*interactive*/ true);
    }
};

ui::Input mouse(int x, int y, bool down, bool pressed) {
    ui::Input u{}; u.mx = x; u.my = y; u.down = down; u.pressed = pressed; return u;
}
ui::Input release(int x, int y) {
    ui::Input u{}; u.mx = x; u.my = y; u.released = true; return u;
}
ui::Input wheel_at(int x, int y, int ticks) {
    ui::Input u{}; u.mx = x; u.my = y; u.wheel = ticks; return u;
}

// ui::interact fires on RELEASE over the rect, so a test that only presses proves a
// widget was drawn and never that it can be pressed.
void click_at(Driver& d, studioshell::SceneWorkspace& ws, int cx, int cy) {
    d.panel(ws, mouse(cx, cy, true, true));
    d.panel(ws, release(cx, cy));
    d.panel(ws, ui::Input{});
}
void click(Driver& d, studioshell::SceneWorkspace& ws, ui::Rect r) {
    if (r.w <= 0 || r.h <= 0) return;      // unreachable: pressing it is not possible
    click_at(d, ws, r.x + r.w / 2, r.y + r.h / 2);
}

// Scroll the body until `id` is reachable, then click it. Below the fold is not
// "cannot be used", it is "scroll to it" — and a helper that silently skipped an
// unreachable control would pass every assertion while the panel was unusable.
bool click_control(Driver& d, studioshell::SceneWorkspace& ws, const char* id,
                   int max_ticks = 60) {
    for (int i = 0; i <= max_ticks; ++i) {
        const ui::Rect r = ws.control_rect(id);
        if (r.w > 0 && r.h > 0) { click(d, ws, r); return true; }
        const ui::Rect vp = ws.inspector_viewport();
        d.panel(ws, wheel_at(vp.x + vp.w / 2, vp.y + vp.h / 2, -1));
    }
    return false;
}

// Scroll down until `id` is in the viewport and return where it landed; an empty rect
// means it never came into reach at all.
ui::Rect scroll_until(Driver& d, studioshell::SceneWorkspace& ws, const char* id,
                      int max_ticks = 60) {
    for (int i = 0; i <= max_ticks; ++i) {
        const ui::Rect r = ws.control_rect(id);
        if (r.w > 0 && r.h > 0) return r;
        const ui::Rect vp = ws.inspector_viewport();
        d.panel(ws, wheel_at(vp.x + vp.w / 2, vp.y + vp.h / 2, -1));
    }
    return ui::Rect{};
}

void scroll_to_top(Driver& d, studioshell::SceneWorkspace& ws) {
    const ui::Rect vp = ws.inspector_viewport();
    for (int i = 0; i < 60; ++i)
        d.panel(ws, wheel_at(vp.x + vp.w / 2, vp.y + vp.h / 2, +1));
}

// Undo through the command registry — the same door Cmd+Z and the palette use, so a
// test cannot pass against an undo the keyboard could not reach.
void cmd_undo(studioshell::SceneWorkspace& ws) {
    ws.register_commands();
    cmd::run("scene.undo", {});
}

std::string scene_of(const studioshell::SceneWorkspace& ws) {
    return sandbox::to_scene(ws.world());
}

// The CANVAS reads platform::InputState, not ui::Input — the widgets and the world
// are two different input paths through the same frame, and a test that only fed the
// widget one would select nothing and then "prove" the inspector empty.
platform::InputState canvas_press(int x, int y, bool pressed) {
    platform::InputState in{};
    in.mouse_x = x;
    in.mouse_y = y;
    in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = pressed;
    in.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = pressed;
    return in;
}

// Select the first actor by clicking it on the canvas — the same way a hand does.
void select_first(Driver& d, studioshell::SceneWorkspace& ws) {
    d.panel(ws, ui::Input{});                       // a draw, so actor_rect() is known
    const ui::Rect r = ws.actor_rect(0);
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    d.panel(ws, ui::Input{}, canvas_press(cx, cy, true));
    d.panel(ws, ui::Input{}, canvas_press(cx, cy, false));
}

} // namespace

// ---- 1. an effect is a component: the checkbox writes the document ----------
static void test_effect_checkboxes_edit_the_scene() {
    Driver d;
    studioshell::SceneWorkspace ws(kPath);
    CHECK(ws.loaded());
    select_first(d, ws);
    CHECK(ws.selected() == 0);

    CHECK(scene_of(ws).find("emitter=") == std::string::npos);
    CHECK(click_control(d, ws, "emitter"));
    CHECK(scene_of(ws).find("emitter=") != std::string::npos);

    CHECK(click_control(d, ws, "light"));
    CHECK(scene_of(ws).find("light=") != std::string::npos);

    // ...and it is ONE undo step each, not a silent mutation. An effect that could not
    // be taken back would be the only edit in this editor that could not.
    CHECK(ws.dirty());
    cmd_undo(ws);
    CHECK(scene_of(ws).find("light=") == std::string::npos);
    CHECK(scene_of(ws).find("emitter=") != std::string::npos);
    cmd_undo(ws);
    CHECK(scene_of(ws).find("emitter=") == std::string::npos);
}

// ---- 2. reach: the body scrolls, and what is off it is not clickable --------
static void test_inspector_scrolls_and_offscreen_is_dead() {
    Driver d;
    studioshell::SceneWorkspace ws(kPath);
    select_first(d, ws);
    CHECK(click_control(d, ws, "emitter"));
    CHECK(click_control(d, ws, "light"));
    CHECK(click_control(d, ws, "sound"));
    CHECK(scene_of(ws).find("sound=") != std::string::npos);

    // With three effects on, the body is taller than the panel it lives in. Before
    // chapter 133 it simply kept drawing, straight over the pinned Save button.
    CHECK(ws.inspector_content_height() > ws.inspector_viewport().h);

    // ui::Context::slider draws its "label: value" ABOVE the rect it is given, so a
    // row must clear the row before it by at least that line. Only the screenshot
    // caught this the first time; it is an arithmetic fact and belongs in an assertion.
    scroll_to_top(d, ws);
    const ui::Rect rate = scroll_until(d, ws, "emitter.rate");
    const ui::Rect box  = ws.control_rect("emitter");
    CHECK(box.w > 0 && rate.w > 0);
    if (box.w > 0 && rate.w > 0)
        CHECK(rate.y - (box.y + box.h) >= ui::theme::sz_caption);

    // Where the emitter checkbox sits when it IS in reach. It is already below the
    // fold at the top of the body — the PLACE palette alone is taller than this
    // viewport — which is the whole reason the body scrolls.
    scroll_to_top(d, ws);
    const ui::Rect emitter_before = scroll_until(d, ws, "emitter");
    CHECK(emitter_before.w > 0);
    // From there, Audition is still below the fold — and control_rect must SAY so
    // rather than hand back coordinates that look usable.
    CHECK(ws.control_rect("sound.audition").w == 0);

    const ui::Rect vp = ws.inspector_viewport();
    for (int i = 0; i < 60; ++i)
        d.panel(ws, wheel_at(vp.x + vp.w / 2, vp.y + vp.h / 2, -1));

    CHECK(ws.control_rect("sound.audition").w > 0);       // scrolled INTO reach
    CHECK(ws.control_rect("emitter").w == 0);             // and the top scrolled OUT

    (void)emitter_before;

    // ...while the control that IS in view works.
    CHECK(click_control(d, ws, "sound.audition"));
    CHECK(ws.take_sounds().size() == 1);
    CHECK(ws.take_sounds().empty());                      // drained, not repeated
}

// ---- 2b. the direction nobody checks: a scrolled-away control is DEAD -------
// Scrolled to the bottom, several body controls have rects ABOVE the viewport — still
// on screen, over the header, invisible only because the renderer clips them. The Play
// button lives up there. Drawing was clipped; hit-testing was not, so one of those
// ghosts would eat the click and Play would do nothing.
static void test_scrolled_body_does_not_eat_the_header() {
    Driver d;
    studioshell::SceneWorkspace ws(kPath);
    select_first(d, ws);
    CHECK(click_control(d, ws, "emitter"));
    CHECK(click_control(d, ws, "light"));
    CHECK(click_control(d, ws, "sound"));

    const ui::Rect vp = ws.inspector_viewport();
    for (int i = 0; i < 60; ++i)
        d.panel(ws, wheel_at(vp.x + vp.w / 2, vp.y + vp.h / 2, -1));
    CHECK(ws.control_rect("emitter").w == 0);        // the top of the body is off-screen

    CHECK(!ws.playing());
    // The Play button, in the header the body is now scrolled behind.
    click_at(d, ws, vp.x + vp.w / 2, vp.y - 30);
    CHECK(ws.playing());
}

// ---- 3. the sound is heard because the actor died --------------------------
static void test_sound_reaches_the_host_during_play() {
    Driver d;
    studioshell::SceneWorkspace ws(kPath);
    d.panel(ws, ui::Input{});
    ws.take_sounds();
    CHECK(!ws.playing());
    ws.toggle_play();
    for (int i = 0; i < 4; ++i) d.panel(ws, ui::Input{});
    // The coin carries sound=523 and the red block overlaps it with `destroy other`.
    const auto heard = ws.take_sounds();
    CHECK(heard.size() == 1);
    if (heard.size() == 1) CHECK(heard[0].freq > 520.0f && heard[0].freq < 526.0f);
}

// ---- 4. the flipbook is a clock, and Restart is not an edit -----------------
static void test_flipbook_controls() {
    Driver d;
    studioshell::SceneWorkspace ws(kPath);
    select_first(d, ws);
    sandbox::World& w = const_cast<sandbox::World&>(ws.world());
    const auto clock = [&] {
        float t = -1;
        w.reg.view<sandbox::Sprite>([&](ecs::Entity, sandbox::Sprite& s) {
            if (s.frames > 1) t = s.t;
        });
        return t;
    };

    // Scroll to Restart FIRST, then advance the clock: every frame animates, so
    // measuring across the scroll would compare two arbitrary points of a 0.5 s cycle
    // and a Restart that did nothing could still look like it went back.
    const ui::Rect restart = scroll_until(d, ws, "flipbook.restart");
    CHECK(restart.w > 0);
    click(d, ws, restart);                            // start from a known zero: the
    for (int i = 0; i < 20; ++i) d.panel(ws, ui::Input{});   // clock wraps every 0.5 s
    const float t = clock();
    CHECK(t > 0.2f);                                  // it animates while STOPPED

    const bool was_dirty = ws.dirty();
    click(d, ws, restart);                            // 3 frames: press, release, idle
    const float after = clock();
    CHECK(after >= 0.0f && after < 4.0f / 60.0f);     // back to ~0, not merely smaller
    CHECK(after < t);
    CHECK(ws.dirty() == was_dirty);                   // ...and it is not a document edit

    // `loop` IS document state, so toggling it must be saved and undoable.
    CHECK(click_control(d, ws, "flipbook.loop"));
    CHECK(scene_of(ws).find("noloop") != std::string::npos);
    cmd_undo(ws);
    CHECK(scene_of(ws).find("noloop") == std::string::npos);
}

// ---- 4b. the speaker: silence is not something you stream ------------------
namespace fake {
int  g_calls = 0, g_samples = 0;
bool open() { return true; }
int  rate() { return 8000; }
void play(const std::int16_t*, int n) { ++g_calls; g_samples += n; }
} // namespace fake

static void test_sound_bank_only_streams_when_something_plays() {
    studioshell::set_audio_device({&fake::open, &fake::rate, &fake::play});
    fake::g_calls = 0;
    {
        studioshell::SoundBank bank;
        bank.pump();
        bank.pump();
        CHECK(fake::g_calls == 0);           // nothing playing: leave the device alone
        CHECK(bank.voices() == 0);

        bank.play(studioshell::Workspace::SoundRequest{440.0f, 50.0f, 0.8f});
        CHECK(bank.device_ok());
        CHECK(bank.voices() == 1);
        bank.pump();
        CHECK(fake::g_calls == 1);

        // A clip is cached by (hz, ms) — the mixer holds pointers into it, so the same
        // request twice must not resynthesise and must not move what it handed out.
        bank.play(studioshell::Workspace::SoundRequest{440.0f, 50.0f, 0.4f});
        CHECK(bank.voices() == 2);

        // 8000 Hz for 50 ms is 400 samples; a 133-sample chunk drains it in 4 pumps.
        for (int i = 0; i < 8; ++i) bank.pump();
        CHECK(bank.voices() == 0);
        const int settled = fake::g_calls;
        bank.pump();
        CHECK(fake::g_calls == settled);     // ...and it goes quiet again
    }
    studioshell::set_audio_device({});
}

// ---- 5. a frame for a human to look at -------------------------------------
static void test_screenshot() {
    Driver d;
    studioshell::SceneWorkspace ws(kPath);
    select_first(d, ws);
    CHECK(click_control(d, ws, "emitter"));
    CHECK(click_control(d, ws, "light"));
    CHECK(click_control(d, ws, "sound"));
    // Stopped, with the effect sections in view: this is what the slice is FOR, and
    // the gizmo is the only thing on screen answering the dir and spread sliders while
    // nothing is flying.
    scroll_to_top(d, ws);
    CHECK(scroll_until(d, ws, "emitter.rate").w > 0);
    d.panel(ws, ui::Input{});
    dump_ppm(d.buf, PW, PH, "scene_inspector.ppm");

    scroll_to_top(d, ws);
    ws.toggle_play();
    for (int i = 0; i < 40; ++i) d.panel(ws, ui::Input{});
    dump_ppm(d.buf, PW, PH, "scene_effects.ppm");
}

int main() {
    std::error_code ec;
    std::filesystem::remove_all(kBase, ec);
    std::filesystem::create_directories(kBase + "/scenes", ec);
    assets::set_base_path(kBase);
    CHECK(write_text(kPath, kFixture));

    test_effect_checkboxes_edit_the_scene();
    test_inspector_scrolls_and_offscreen_is_dead();
    test_scrolled_body_does_not_eat_the_header();
    test_sound_reaches_the_host_during_play();
    test_flipbook_controls();
    test_sound_bank_only_streams_when_something_plays();
    test_screenshot();

    assets::set_base_path(".");
    std::filesystem::remove_all(kBase, ec);
    if (g_failures == 0) std::printf("scene_workspace: all tests passed\n");
    else                 std::printf("scene_workspace: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
