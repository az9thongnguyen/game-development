// =============================================================================
//  tests/test_farm_scene.cpp  —  the farm, played with no window
// =============================================================================
//  A Scene sees only a platform::InputState and a Renderer2D, so the game can be
//  driven headless: press the keys, check the world changed, and check the SCREEN
//  changed with it. The second half matters as much as the first — a simulation that
//  advances while the render ignores it looks exactly like a frozen game.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/renderer2d.hpp"
#include "engine/text/font.hpp"
#include "games/farm/farm_scene.hpp"

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

constexpr int LW = 640, LH = 360, SS = 2;
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

// Mean luminance of the play area (below the HUD strip), for the day/night check.
double brightness(const std::vector<std::uint32_t>& b) {
    double sum = 0;
    std::size_t n = 0;
    for (int y = 40 * SS; y < PH; ++y)
        for (int x = 0; x < PW; ++x) {
            const std::uint32_t p = b[static_cast<std::size_t>(y) * PW + x];
            sum += ((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF);
            ++n;
        }
    return n ? sum / static_cast<double>(n) : 0.0;
}

std::uint64_t fingerprint(const std::vector<std::uint32_t>& b) {
    std::uint64_t h = 1469598103934665603ull;
    for (auto p : b) h = (h ^ p) * 1099511628211ull;
    return h;
}

platform::InputState key(platform::Key k) {
    platform::InputState in{};
    in.key_pressed[static_cast<int>(k)] = true;
    in.key_down[static_cast<int>(k)] = true;
    return in;
}

} // namespace

int main() {
    assets::set_base_path(ASSET_ROOT "/assets");
    auto bytes = assets::load_file("fonts/Inter.ttf");
    CHECK(bytes.has_value());
    if (!bytes) return 1;
    auto font = text::Font::load_from_bytes(std::move(*bytes));
    CHECK(font != nullptr);
    if (!font) return 1;

    farm::FarmScene scene;
    CHECK(scene.ready());
    if (!scene.ready()) { std::printf("  problem: %s\n", scene.problem().c_str()); return 1; }

    // The manifest's map and data files actually loaded into a playable world.
    CHECK(scene.world().energy == farm::kMaxEnergy);
    CHECK(scene.world().day == 1);
    CHECK(!scene.world().npcs.empty());              // Anna is on the map
    CHECK(scene.world().npcs[0].x != 0 || scene.world().npcs[0].y != 0);

    std::vector<std::uint32_t> buf(static_cast<std::size_t>(PW) * PH, 0);
    platform::Framebuffer fb{buf.data(), PW, PH, PW};
    const platform::InputState idle{};

    const auto render = [&](const platform::InputState& in) {
        for (auto& p : buf) p = 0;
        gfx::Renderer2D r(fb, SS);
        const engine::Context ctx{r, in, 1.0 / 60.0, 0.0, 0.0, font.get()};
        scene.render(ctx);
    };

    scene.update(1.0 / 60.0, idle);
    render(idle);
    const std::uint64_t first = fingerprint(buf);
    dump_ppm(buf, "farm_day.ppm");

    // The world is on screen: the play area is not one flat colour, and the HUD strip
    // at the top is a different thing from the field below it.
    {
        // Counting DISTINCT colours rather than probing fixed pixels: the world is
        // centred in the viewport, so where any particular tile lands depends on the
        // camera, and a probe would be testing the camera by accident.
        std::vector<std::uint32_t> seen;
        for (int y = 60 * SS; y < PH; y += 8 * SS)
            for (int x = 0; x < PW; x += 8 * SS) {
                const std::uint32_t p = buf[static_cast<std::size_t>(y) * PW + x];
                bool found = false;
                for (std::uint32_t s : seen) if (s == p) { found = true; break; }
                if (!found) seen.push_back(p);
            }
        CHECK(seen.size() >= 4);                    // grass, path, trees, water at least
        CHECK(brightness(buf) > 20.0);              // and not a black screen
    }

    // ---- farming with the keyboard ----
    // Face down, then hoe / water the tile in front. The tool selection and the action
    // are the same two keys a player presses.
    scene.update(1.0 / 60.0, key(platform::Key::S));
    scene.update(1.0 / 60.0, key(platform::Key::Num1));
    scene.update(1.0 / 60.0, key(platform::Key::Z));
    int tilled = 0;
    for (const auto& [k, s] : scene.world().soil) if (s.tilled) ++tilled;
    CHECK(tilled == 1);

    scene.update(1.0 / 60.0, key(platform::Key::Num2));
    scene.update(1.0 / 60.0, key(platform::Key::Z));
    int watered = 0;
    for (const auto& [k, s] : scene.world().soil) if (s.watered) ++watered;
    CHECK(watered == 1);

    scene.update(1.0 / 60.0, key(platform::Key::Num3));
    scene.update(1.0 / 60.0, key(platform::Key::Z));
    int planted = 0;
    for (const auto& [k, s] : scene.world().soil) if (s.crop >= 0) ++planted;
    CHECK(planted == 1);
    CHECK(scene.world().energy < farm::kMaxEnergy);

    // Working the field is VISIBLE. Without this the simulation could advance behind
    // a screen that never changed and every check above would still pass.
    render(idle);
    CHECK(fingerprint(buf) != first);
    dump_ppm(buf, "farm_planted.ppm");
    const double lit = brightness(buf);

    // ---- night falls ----
    // Run the clock to the small hours and the world darkens. The tint is what makes
    // the clock a resource rather than a number in the corner.
    // Stop at 22:00 rather than running a fixed frame count: past 02:00 the player
    // collapses and the clock resets, which would leave this asserting on a fresh
    // morning and reporting the tint as broken.
    int frames = 0;
    while (scene.world().minute < 22 * 60 && frames < 200000) {
        scene.update(1.0 / 60.0, idle);
        ++frames;
    }
    CHECK(scene.world().minute >= 22 * 60);
    CHECK(scene.world().day == 1);            // ...and we never rolled over
    render(idle);
    dump_ppm(buf, "farm_night.ppm");
    CHECK(brightness(buf) < lit);

    if (g_failures == 0) std::printf("farm_scene: all tests passed\n");
    else                 std::printf("farm_scene: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
