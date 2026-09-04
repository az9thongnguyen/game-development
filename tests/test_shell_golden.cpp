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
#include "engine/hub/hub_build.hpp"
#include "engine/renderer2d.hpp"
#include "engine/scene.hpp"
#include "engine/text/font.hpp"
#include "engine/ui/theme.hpp"
#include "games/studio_shell/studio_shell_scene.hpp"

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

    studioshell::StudioShellScene scene(kProject);

    std::vector<std::uint32_t> buf(static_cast<std::size_t>(PW) * PH, 0);
    platform::Framebuffer fb{buf.data(), PW, PH, PW};
    platform::InputState  input{};                 // no mouse over anything, no keys

    const char* names[] = {"hub", "guide", "learn", "about"};
    for (int section = 0; section < 4; ++section) {
        // Section 0 is the default; step to the next one with a Tab edge each round.
        if (section > 0) {
            platform::InputState tab{};
            tab.key_pressed[static_cast<int>(platform::Key::Tab)] = true;
            scene.update(1.0 / 60.0, tab);
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
        // ...and the content area behind it is the window background.
        CHECK(at(1270, 700) == th::bg);

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
        for (int i = 0; i < 4; ++i) {
            const int ry = (24 + 20 + 24) + i * (32 + 4) + 16;   // middle of nav item i
            if (at(20, ry) == th::accent) ++accent_rows;
        }
        CHECK(accent_rows == 1);

        dump_ppm(buf, PW, PH, (std::string("shell_") + names[section] + ".ppm").c_str());
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

            studioshell::StudioShellScene sc(kProject);      // fresh: starts on Hub
            sc.update(1.0 / 60.0, input);
            sc.render(c);

            const auto px = [&](int lx, int ly) { return b[(ly * sz.ss) * pw + (lx * sz.ss)]; };

            // The rail spans the FULL height whatever that height is — the bug a
            // hard-coded panel height produces is a rail that stops short.
            CHECK(px(60, 2) == th::elevated);
            CHECK(px(60, sz.h - 3) == th::elevated);
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

        studioshell::StudioShellScene sc(kProject);

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
            studioshell::StudioShellScene sc2(kProject);
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
