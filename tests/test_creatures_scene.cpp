// =============================================================================
//  tests/test_creatures_scene.cpp  —  the second game, played with no window
// =============================================================================
//  Same shape as the farm's scene test and for the same reason: a Scene sees only a
//  `platform::InputState` and a `Renderer2D`, so it can be driven headless — press
//  the button, check the world changed, and check the SCREEN changed with it.
//
//  The half that keeps finding bugs is the one that taps WHERE A FINGER TAPS. Four
//  chapters of this project have shipped a control that was drawn perfectly and
//  wired to nothing (126, 127, 132, 135), and every one of them was found by a test
//  that clicked a rectangle rather than calling the function behind it. So this file
//  never calls `choose_cell`; it puts the pointer on the button.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/renderer2d.hpp"
#include "engine/text/font.hpp"
#include "games/creatures/creatures_scene.hpp"

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
        std::fprintf(f, "P6\n%d %d\n255\n", PW, static_cast<int>(buf.size()) / PW);
        for (auto p : buf) {
            const unsigned char rgb[3] = {static_cast<unsigned char>((p >> 16) & 0xFF),
                                          static_cast<unsigned char>((p >> 8) & 0xFF),
                                          static_cast<unsigned char>(p & 0xFF)};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
    }
}

int distinct_colours(const std::vector<std::uint32_t>& b, int step = 8) {
    std::map<std::uint32_t, int> seen;
    for (int y = 0; y < PH; y += step * SS)
        for (int x = 0; x < PW; x += step * SS)
            ++seen[b[static_cast<std::size_t>(y) * PW + x]];
    return static_cast<int>(seen.size());
}

double brightness(const std::vector<std::uint32_t>& b) {
    double sum = 0;
    for (auto p : b) sum += ((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF);
    return b.empty() ? 0 : sum / static_cast<double>(b.size());
}

// Pixels in a logical rect that are not the most common colour there.
int ink(const std::vector<std::uint32_t>& b, creature::Box r) {
    std::map<std::uint32_t, int> hist;
    for (int py = r.y * SS; py < (r.y + r.h) * SS && py < PH; ++py)
        for (int px = r.x * SS; px < (r.x + r.w) * SS && px < PW; ++px)
            ++hist[b[static_cast<std::size_t>(py) * PW + px]];
    int best = -1, total = 0;
    for (const auto& [c, n] : hist) { total += n; if (n > best) best = n; }
    return total - best;
}

// How far the LABEL is from the FILL, in luminance. `ink() > 0` was not enough and
// this is the check that would have caught the first version of the battle menu:
// every label was present, every label was near-black text on a near-black button,
// and counting non-background pixels said everything was fine.
//
// The dominant colour in the box is the fill; the answer is the largest luminance
// distance from it among colours appearing at least four times. A percentage
// threshold does NOT work here and the first attempt at this used one: the font is
// anti-aliased, so a five-letter label is spread over dozens of near-colours and not
// one of them reaches 1% of the button. Four pixels is enough to exclude a stray
// sprite edge and low enough to see a one-character label.
int contrast(const std::vector<std::uint32_t>& b, creature::Box r) {
    std::map<std::uint32_t, int> hist;
    for (int py = r.y * SS; py < (r.y + r.h) * SS && py < PH; ++py)
        for (int px = r.x * SS; px < (r.x + r.w) * SS && px < PW; ++px)
            ++hist[b[static_cast<std::size_t>(py) * PW + px]];
    int total = 0, best = -1;
    std::uint32_t fill = 0;
    for (const auto& [c, n] : hist) { total += n; if (n > best) { best = n; fill = c; } }
    if (total == 0) return 0;
    const auto lum = [](std::uint32_t c) {
        return (static_cast<int>((c >> 16) & 0xFF) * 299 +
                static_cast<int>((c >> 8) & 0xFF) * 587 +
                static_cast<int>(c & 0xFF) * 114) / 1000;
    };
    int gap = 0;
    for (const auto& [c, n] : hist) {
        if (n < 4) continue;
        gap = std::max(gap, std::abs(lum(c) - lum(fill)));
    }
    return gap;
}

// How many DISTINCT colours a box holds, counting only those with a real footprint.
// `ink() > 0` is not enough to say "a sprite is here": the battle screen's horizon
// runs through the player's creature box, so two flat bands already answer yes — and
// a mutation that stopped drawing the sprite passed a 200-pixel ink threshold on the
// seam alone. A 16 px creature has an outline, a body, an accent and two eyes.
int palette_count(const std::vector<std::uint32_t>& b, creature::Box r) {
    std::map<std::uint32_t, int> hist;
    for (int py = r.y * SS; py < (r.y + r.h) * SS && py < PH; ++py)
        for (int px = r.x * SS; px < (r.x + r.w) * SS && px < PW; ++px)
            ++hist[b[static_cast<std::size_t>(py) * PW + px]];
    int n = 0;
    for (const auto& [c, count] : hist) if (count >= 8) ++n;
    return n;
}

void clear_file(const char* rel) {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(ASSET_ROOT "/assets") / rel, ec);
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

    // A save from a previous run would make every "a new game starts at home" check
    // depend on the last test that ran.
    clear_file("saves/creatures/slot1.sav");

    creature::CreaturesScene scene;
    CHECK(scene.ready());
    if (!scene.ready()) { std::printf("  problem: %s\n", scene.problem().c_str()); return 1; }

    std::vector<std::uint32_t> buf(static_cast<std::size_t>(PW) * PH, 0);
    platform::Framebuffer fb{buf.data(), PW, PH, PW};

    const auto render = [&](const platform::InputState& in) {
        for (auto& p : buf) p = 0;
        gfx::Renderer2D r(fb, SS);
        const engine::Context ctx{r, in, 1.0 / 60.0, 0.0, 0.0, font.get()};
        scene.render(ctx);
    };

    // A pointer press is TWO frames: `pressed` is an edge, and a helper that only
    // ever sends the edge would leave `down` false — which is what a d-pad reads.
    const auto tap = [&](int x, int y, int frames = 2) {
        for (int i = 0; i < frames; ++i) {
            platform::InputState in{};
            in.mouse_x = x; in.mouse_y = y;
            in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
            in.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = (i == 0);
            scene.update(1.0 / 60.0, in);
            render(in);
        }
        platform::InputState up{};
        up.mouse_x = x; up.mouse_y = y;
        scene.update(1.0 / 60.0, up);
    };
    const auto centre = [](creature::Box b) {
        return std::pair<int, int>{b.x + b.w / 2, b.y + b.h / 2};
    };

    const platform::InputState idle{};
    scene.update(1.0 / 60.0, idle);
    render(idle);

    // ---- 1. the route is on the screen -----------------------------------------
    dump_ppm(buf, "creatures_route.ppm");
    CHECK(brightness(buf) > 20.0);                 // not a black window
    CHECK(distinct_colours(buf) >= 8);             // grass, path, trees, HUD, sprite...
    CHECK(scene.world().phase == creature::Phase::Overworld);
    CHECK(scene.world().party.count == 1);

    // ---- 2. the d-pad is DRAWN and it WORKS ------------------------------------
    // Not "walk() moves the player" — that is test_creature_world's job. This is:
    // a finger on the pixel the renderer drew a button at makes the player move.
    {
        const creature::Layout l = scene.controls();
        CHECK(l.pad_visible());
        CHECK(ink(buf, l.right) > 0);              // ...and something is drawn there
        CHECK(ink(buf, l.act) > 0);
        CHECK(ink(buf, l.save) > 0);

        const int before_x = scene.world().px;
        const auto [rx, ry] = centre(l.right);
        // Held, not tapped: a direction is a hold, and the scene rate-limits steps.
        for (int i = 0; i < 40 && scene.world().px == before_x; ++i) {
            platform::InputState in{};
            in.mouse_x = rx; in.mouse_y = ry;
            in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
            scene.update(1.0 / 30.0, in);
        }
        CHECK(scene.world().px > before_x);
    }

    // ---- 3. walk into the grass until something jumps out -----------------------
    // Drive with the POINTER on the d-pad the whole way. The route's near grass is
    // east and north of home, so this is a walk a player would take.
    {
        int guard = 0;
        while (scene.world().phase == creature::Phase::Overworld && guard++ < 4000) {
            const creature::Layout l = scene.controls();
            // A snake through the grass: east until blocked, then one step north.
            const creature::Box& dir = (guard / 60) % 2 ? l.up : l.right;
            const auto [x, y] = centre(dir);
            platform::InputState in{};
            in.mouse_x = x; in.mouse_y = y;
            in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
            scene.update(1.0 / 30.0, in);
        }
        CHECK(scene.world().phase == creature::Phase::Battle);
        CHECK(scene.world().wild_species > 0);
        std::printf("  ambushed after %d frames by species %d at level %d\n",
                    guard, scene.world().wild_species, scene.world().wild_level);
    }

    // ---- 4. the battle screen, and its menu ------------------------------------
    render(idle);
    dump_ppm(buf, "creatures_battle.ppm");
    CHECK(scene.mode() == creature::Mode::Menu);
    {
        const creature::Layout l = scene.controls();
        for (int i = 0; i < 4; ++i) {
            CHECK(!l.cell[i].empty());
            CHECK(ink(buf, l.cell[i]) > 0);        // every menu button has a label...
            // ...and the label is READABLE. The first version of this screen picked
            // one ink colour and hoped, which came out near-black on near-black:
            // every label present, every label invisible, and `ink > 0` said fine.
            if (contrast(buf, l.cell[i]) < 40)
                std::printf("      menu cell %d contrast %d\n", i, contrast(buf, l.cell[i]));
            CHECK(contrast(buf, l.cell[i]) >= 40);
        }
        CHECK(l.cell[4].empty());                  // ...and the party-only slots do not
        CHECK(l.cell[5].empty());

        // FIGHT opens the move list, and the move list is four DIFFERENT buttons.
        const auto [fx, fy] = centre(l.cell[0]);
        tap(fx, fy);
        CHECK(scene.mode() == creature::Mode::Moves);
        render(idle);
        dump_ppm(buf, "creatures_moves.ppm");
        const creature::Layout mv = scene.controls();
        CHECK(!mv.back.empty());
        CHECK(contrast(buf, mv.back) >= 40);
        // A move button is tinted by its TYPE, so the ink has to be chosen per button
        // rather than once — a light fire orange and a dark grey slot cannot share it.
        for (int i = 0; i < creature::kMoveSlots; ++i) {
            CHECK(ink(buf, mv.cell[i]) > 0);
            if (contrast(buf, mv.cell[i]) < 40)
                std::printf("      move cell %d contrast %d\n", i, contrast(buf, mv.cell[i]));
            CHECK(contrast(buf, mv.cell[i]) >= 40);
        }
        // ...and Back must not be drawn on top of the thing the screen is about. It
        // was, until a rendered frame showed it sitting on the player's creature.
        CHECK(mv.back.y >= mv.panel.y);

        // BOTH creatures are actually on the screen. The rects come from the layout,
        // so this reads the same numbers the renderer used instead of re-deriving
        // them and agreeing with itself.
        CHECK(!mv.mine.empty() && !mv.theirs.empty());
        if (palette_count(buf, mv.mine) < 4)
            std::printf("      your creature box holds %d colours\n", palette_count(buf, mv.mine));
        CHECK(palette_count(buf, mv.mine) >= 4);
        CHECK(palette_count(buf, mv.theirs) >= 4);

        // An EMPTY move slot does not spend the turn. emberpup at level 5 knows two
        // moves, so slots 2 and 3 are empty; tapping one must leave the move list up
        // and the battle where it was.
        {
            const int turn_before = scene.world().battle.turn;
            const auto [ex, ey] = centre(mv.cell[3]);
            tap(ex, ey);
            CHECK(scene.mode() == creature::Mode::Moves);
            CHECK(scene.world().battle.turn == turn_before);
            CHECK(scene.message() == "No move there");
        }

        // Back really goes back — the guard in the other direction.
        const auto [bx, by] = centre(mv.back);
        tap(bx, by);
        CHECK(scene.mode() == creature::Mode::Menu);

        // PARTY is a third mode with six slots.
        const creature::Layout m2 = scene.controls();
        const auto [px, py] = centre(m2.cell[2]);
        tap(px, py);
        CHECK(scene.mode() == creature::Mode::Party);
        CHECK(!scene.controls().cell[5].empty());
        const auto [b2x, b2y] = centre(scene.controls().back);
        tap(b2x, b2y);
        CHECK(scene.mode() == creature::Mode::Menu);
    }

    // ---- 5. fight it out with the pointer --------------------------------------
    {
        const int hp_before = scene.world().battle.side[1].now().hp;
        int guard = 0;
        while (scene.world().phase == creature::Phase::Battle && guard++ < 80) {
            const creature::Layout l = scene.controls();
            if (scene.mode() == creature::Mode::Menu) {
                const auto [x, y] = centre(l.cell[0]);      // Fight
                tap(x, y);
            } else if (scene.mode() == creature::Mode::Moves) {
                const auto [x, y] = centre(l.cell[0]);      // the first move
                tap(x, y);
            } else break;
        }
        CHECK(guard < 80);
        CHECK(scene.world().phase != creature::Phase::Battle);
        // Something actually happened to the other side.
        CHECK(scene.world().battle.side[1].now().hp < hp_before ||
              scene.world().phase == creature::Phase::Blackout);
        std::printf("  battle ended after %d taps, phase %d\n", guard,
                    static_cast<int>(scene.world().phase));
    }

    // ---- 6. the end-of-battle screen has exactly one button ---------------------
    {
        CHECK(scene.mode() == creature::Mode::Ack);
        render(idle);
        dump_ppm(buf, "creatures_ack.ppm");
        const creature::Layout l = scene.controls();
        CHECK(!l.ack.empty());
        CHECK(contrast(buf, l.ack) >= 40);
        for (int i = 0; i < 6; ++i) CHECK(l.cell[i].empty());   // no stale move buttons
        const auto [x, y] = centre(l.ack);
        tap(x, y);
        CHECK(scene.world().phase == creature::Phase::Overworld);
        CHECK(scene.mode() == creature::Mode::Overworld);
    }

    // ---- 7. the save button ------------------------------------------------------
    {
        clear_file("saves/creatures/slot1.sav");
        CHECK(!assets::load_file("saves/creatures/slot1.sav"));
        render(idle);
        const creature::Layout l = scene.controls();
        const auto [x, y] = centre(l.save);
        tap(x, y);
        const auto saved = assets::load_file("saves/creatures/slot1.sav");
        CHECK(saved.has_value());
        CHECK(scene.message() == "Saved");

        // ...and it comes back. Move first, so "loaded" is distinguishable from
        // "never changed".
        const int sx = scene.world().px, sy = scene.world().py;
        {
            const auto [ux, uy] = centre(l.left);
            for (int i = 0; i < 30; ++i) {
                platform::InputState in{};
                in.mouse_x = ux; in.mouse_y = uy;
                in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
                scene.update(1.0 / 30.0, in);
            }
        }
        CHECK(scene.world().px != sx || scene.world().py != sy);
        CHECK(scene.load_game());
        CHECK(scene.world().px == sx && scene.world().py == sy);
    }

    // ---- 7b. the grid has a cooldown --------------------------------------------
    // One tile per press-and-hold interval, not one per frame. Without it a finger
    // resting on the pad crosses the map in a third of a second and every patch of
    // grass is one step wide.
    {
        const int before = scene.world().px + scene.world().py * 1000;
        constexpr int kFrames = 24;
        constexpr double kDt = 1.0 / 60.0;
        int moves = 0, last = scene.world().px;
        const creature::Layout l = scene.controls();
        const auto [rx, ry] = centre(l.right);
        for (int i = 0; i < kFrames; ++i) {
            platform::InputState in{};
            in.mouse_x = rx; in.mouse_y = ry;
            in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
            scene.update(kDt, in);
            if (scene.world().px != last) { ++moves; last = scene.world().px; }
        }
        (void)before;
        CHECK(moves >= 1);
        // 24 frames at 1/60 s is 0.4 s; the step interval is 0.11 s, so at most four.
        CHECK(moves <= 5);
        std::printf("  %d tiles in %d frames\n", moves, kFrames);
    }

    // ---- 8. the keyboard still works ---------------------------------------------
    // Both directions matter: the on-screen controls were added so a phone can play,
    // not so a keyboard stops working.
    {
        const int before = scene.world().px;
        for (int i = 0; i < 30; ++i) {
            platform::InputState in{};
            in.key_down[static_cast<int>(platform::Key::D)] = true;
            scene.update(1.0 / 30.0, in);
        }
        CHECK(scene.world().px > before);
    }

    // ---- 9. a control the pointer is NOT on does nothing --------------------------
    // The reverse of every check above. Without it, a hit test that returned true for
    // everything would pass the whole file.
    {
        const creature::Layout l = scene.controls();
        const int before = scene.world().px;
        for (int i = 0; i < 30; ++i) {
            platform::InputState in{};
            in.mouse_x = l.right.x + l.right.w + 40;      // just past the button
            in.mouse_y = l.right.y;
            in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
            scene.update(1.0 / 30.0, in);
        }
        CHECK(scene.world().px == before);
    }

    clear_file("saves/creatures/slot1.sav");
    if (g_failures == 0) std::printf("test_creatures_scene: all checks passed\n");
    else                 std::printf("test_creatures_scene: %d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
