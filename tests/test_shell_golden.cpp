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

void dump_ppm(const std::vector<std::uint32_t>& buf, const char* name) {
    if (FILE* f = std::fopen(name, "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", PW, PH);
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

        dump_ppm(buf, (std::string("shell_") + names[section] + ".ppm").c_str());
    }

    if (g_failures == 0) std::printf("shell_golden: all tests passed\n");
    else                 std::printf("shell_golden: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
