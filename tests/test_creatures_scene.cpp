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
#include "engine/ui/theme.hpp"
#include "engine/ui/touch.hpp"
#include "games/creatures/creatures_scene.hpp"
#include "gbaas/gbaas.h"
#include "games/creatures/replay.hpp"

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
    // ...and a recording from a previous run would let section 5b pass without the
    // game having written anything at all.
    clear_file("saves/creatures/last_battle.crep");

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
    CHECK(scene.tile_frame("water") == 0);
    scene.update(0.25, idle);
    CHECK(scene.tile_frame("water") == 1);
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

    // ---- 5b. the fight left a file behind, and a stranger can check it ----------
    // The strongest claim this project makes about the creature sim is that the same
    // actions produce the same battle on any machine. Everything that checks it lives
    // inside a test process that also produced the battle. This is the other half:
    // the GAME, played through the pointer, writes a recording, and something that
    // never saw the battle re-plays it and agrees turn by turn.
    {
        const auto raw = assets::load_file("saves/creatures/last_battle.crep");
        CHECK(raw.has_value());
        if (raw) {
            creature::Replay r;
            std::string why;
            const bool read_ok =
                creature::read_replay(scene.dex(), std::string(raw->begin(), raw->end()),
                                      r, &why);
            if (!read_ok) std::printf("      %s\n", why.c_str());
            CHECK(read_ok);
            CHECK(r.turns.size() >= 1);
            CHECK(r.rules == creature::rules_hash(scene.dex()));
            const creature::Verdict v = creature::verify(scene.dex(), r);
            if (!v.ok) std::printf("      %s\n", v.why.c_str());
            CHECK(v.ok);
            // ...and it is a recording of THIS fight, not of a battle left over from
            // some previous run of the test: the state it replays to is the state the
            // scene is holding.
            CHECK(creature::hash(v.final) == creature::hash(scene.world().battle));
            std::printf("  the fight wrote %zu turns to last_battle.crep\n", r.turns.size());
        }
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

        // Two real fingers reach two different controls in the same frame. Save is
        // checked from the file, while Act is checked from the later message it owns;
        // together they prove neither contact was collapsed into synthesized mouse.
        platform::InputState two{};
        CHECK(two.begin_touch(31, l.save.x + l.save.w / 2,
                                  l.save.y + l.save.h / 2));
        CHECK(two.begin_touch(32, l.act.x + l.act.w / 2,
                                  l.act.y + l.act.h / 2));
        scene.update(1.0 / 60.0, two);
        CHECK(assets::load_file("saves/creatures/slot1.sav").has_value());
        CHECK(scene.message() == "Nothing here" || scene.message() == "Your party is rested");

        clear_file("saves/creatures/slot1.sav");
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

    // ---- 10. the rated match, with no server anywhere (chapter 146) ---------------
    // Everything the network can be asked is asked by `test_creature_pvp_live` against
    // a real one. What only THIS file can answer is whether a hand can reach any of it:
    // is the button drawn where a finger lands, does tapping it change the screen, and
    // does the way out work. Pointed at a closed port on purpose — a session that
    // cannot connect is the state a player on a train is in, and it has to say so.
    {
        // Walk back to a calm overworld first: a wild battle must refuse to start one.
        while (scene.mode() != creature::Mode::Overworld) {
            const creature::Layout l = scene.controls();
            const auto [ax, ay] = centre(l.ack.empty() ? l.cell[3] : l.ack);
            tap(ax, ay);
            render(idle);
        }
        CHECK(scene.online() == nullptr);

        const creature::Layout over = scene.controls();
        // Drawn, and clear of the two buttons beside it — the whole reason it is a row
        // above `save` is that starting a rated match by mis-reaching is the worst
        // stray tap in the game.
        CHECK(!over.online.empty());
        CHECK(over.online.w >= 44 && over.online.h >= 44);
        CHECK(!over.online.overlaps(over.act));
        CHECK(!over.online.overlaps(over.save));
        CHECK(!over.online.overlaps(over.up));

        // ---- the layout, asked directly (no session, no server) ----------------
        // Three claims that are cheap here and expensive anywhere else, each of which
        // survived a mutation until it was written down.
        {
            using creature::Box;
            // An empty box overlaps nothing — the rule `contains` already follows, and
            // what every "these two controls do not sit on each other" check rests on.
            const Box ten{0, 0, 10, 10};
            CHECK(!Box{}.overlaps(ten));
            CHECK(!ten.overlaps(Box{}));
            // An empty box with a POSITION is the case that matters and the one a
            // default-constructed `Box{}` cannot show: at the origin the arithmetic
            // already answers false, so the guard looks load-bearing and is not. A
            // layout's absent control is `Box{x, y, 0, 0}` as often as it is `Box{}`.
            const Box degenerate{5, 5, 0, 0};
            CHECK(!degenerate.overlaps(ten));
            CHECK(!ten.overlaps(degenerate));
            const Box corner{9, 9, 10, 10}, beside{10, 0, 10, 10};
            CHECK(ten.overlaps(corner));
            CHECK(!ten.overlaps(beside));   // touching edges is not overlap

            // On a screen too short for another row, the online button is ABSENT rather
            // than placed off the top edge. An empty box is hit by nothing, which is why
            // there is no `bool has_online` beside it.
            // Heights where the pad ACTUALLY exists. The first version of this swept
            // 120..400 and the pad does not appear below ~360, so almost every pass hit
            // the `continue` and the loop asserted nothing at all — a sweep that never
            // reaches its body is decoration.
            int swept = 0;
            for (int h = 300; h <= 900; h += 3) {
                const creature::Layout t = creature::layout(480, h, creature::Mode::Overworld);
                if (t.online.empty()) continue;
                ++swept;
                // Inside the screen, off the margin, and clear of both neighbours. `>= 0`
                // alone is not the claim: a button one pixel from the top edge is inside
                // the framebuffer and outside a thumb's reach, and it passed that check.
                CHECK(t.online.y >= touch::kMargin);
                CHECK(t.online.y + t.online.h <= h);
                CHECK(!t.online.overlaps(t.save));
                CHECK(!t.online.overlaps(t.act));
                CHECK(!t.online.overlaps(t.up));
            }
            CHECK(swept > 50);   // ...and the loop above ran

            // The search screen has no creature rects: there is nothing to draw yet, and
            // a renderer that forgets to check must draw nothing rather than draw at 0,0.
            const creature::Layout on = creature::layout(640, 360, creature::Mode::Online);
            CHECK(on.mine.empty());
            CHECK(on.theirs.empty());
            CHECK(on.cell[0].empty());
            CHECK(!on.back.empty());
        }

        // A rated match cannot be started from inside a wild one — one fight at a time.
        // Checked by walking into a fight rather than by faking a phase.
        {
            gbaas::Config unused;
            unused.base_url = "http://127.0.0.1:9";
            unused.api_key  = "pk_demo_creatures";
            for (int i = 0; i < 400 && scene.mode() == creature::Mode::Overworld; ++i) {
                platform::InputState in{};
                in.mouse_x = scene.controls().right.x + 4;
                in.mouse_y = scene.controls().right.y + 4;
                in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
                scene.update(1.0 / 60.0, in);
                render(in);
            }
            CHECK(scene.mode() != creature::Mode::Overworld);   // in a battle
            CHECK(!scene.start_online(unused));
            CHECK(scene.online() == nullptr);
            while (scene.mode() != creature::Mode::Overworld) {
                const creature::Layout l = scene.controls();
                const auto [ax, ay] = centre(l.ack.empty() ? l.cell[3] : l.ack);
                tap(ax, ay);
                render(idle);
            }
        }

        gbaas::Config nowhere;
        nowhere.base_url = "http://127.0.0.1:9";   // discard: refused immediately
        nowhere.api_key  = "pk_demo_creatures";
        CHECK(scene.start_online(nowhere));
        CHECK(scene.online() != nullptr);
        CHECK(scene.mode() == creature::Mode::Online);

        // The d-pad is GONE while a session is up. Walking off while a server holds you
        // in its queue is how a player gets matched with somebody who is not looking.
        const creature::Layout on = scene.controls();
        CHECK(on.up.empty() && on.down.empty() && on.left.empty() && on.right.empty());
        CHECK(on.online.empty());
        CHECK(!on.back.empty());                       // ...and Cancel is there
        CHECK(!on.back.overlaps(on.log));

        // A second session is refused while one is running — both directions, because a
        // guard that never lets go is the failure this project keeps finding.
        CHECK(!scene.start_online(nowhere));

        // The world does not move while the match screen is up, even with the d-pad's
        // old coordinates pressed. This is the reverse test that matters most here:
        // an empty Box is hit by nothing, and that is the only thing stopping it.
        {
            const int before = scene.world().px;
            for (int i = 0; i < 30; ++i) {
                platform::InputState in{};
                in.mouse_x = over.right.x + over.right.w / 2;
                in.mouse_y = over.right.y + over.right.h / 2;
                in.mouse_down[static_cast<int>(platform::MouseButton::Left)] = true;
                scene.update(1.0 / 30.0, in);
            }
            CHECK(scene.world().px == before);
        }

        // Pump until the connection is refused. The failure must reach the SCREEN — a
        // session that fails silently leaves a player staring at "Looking for an
        // opponent..." forever.
        for (int i = 0; i < 400 && scene.mode() == creature::Mode::Online; ++i) {
            scene.update(1.0 / 60.0, idle);
            render(idle);
        }
        CHECK(scene.mode() == creature::Mode::Ack);
        CHECK(scene.online() != nullptr);
        CHECK(scene.online()->state() == creature::PvpClient::State::Failed);
        CHECK(!scene.online()->problem().empty());

        // The Continue button is DRAWN, not merely present in the layout. The first
        // version of this screen chose its text and then `break`ed out of the switch,
        // skipping the draw, the button and the return — so it was hittable and
        // INVISIBLE, and every assertion above passed, because they all tap the rect the
        // layout reports and the layout was right. Only a rendered frame showed it.
        render(idle);
        {
            const creature::Layout a = scene.controls();
            CHECK(!a.ack.empty());
            int accent = 0;
            for (int yy = a.ack.y * SS; yy < (a.ack.y + a.ack.h) * SS; ++yy)
                for (int xx = a.ack.x * SS; xx < (a.ack.x + a.ack.w) * SS; ++xx)
                    if (buf[static_cast<std::size_t>(yy) * PW + static_cast<std::size_t>(xx)] ==
                        ui::theme::accent)
                        ++accent;
            CHECK(accent > 0);
            // ...and the panel says something. Ink where the message goes, counted the
            // same way, because a blank strip and a strip with a sentence on it are the
            // same rectangle otherwise.
            // From +20 down: `message_` is drawn at the TOP of the log rect, so an ink
            // count over the whole box is satisfied by a leftover toast and says nothing
            // about the result line. It survived a mutation that deleted the result line
            // entirely until this offset was here.
            int ink = 0;
            for (int yy = (a.log.y + 20) * SS; yy < (a.log.y + a.log.h) * SS; ++yy)
                for (int xx = a.log.x * SS; xx < (a.log.x + a.log.w) * SS; ++xx) {
                    const std::uint32_t px =
                        buf[static_cast<std::size_t>(yy) * PW + static_cast<std::size_t>(xx)];
                    if (px != 0 && px != gfx::rgba(0x10, 0x14, 0x1a, 235)) ++ink;
                }
            CHECK(ink > 0);

            // ...and NOTHING is drawn where the two creatures would go. A session that
            // failed before the parties were exchanged has no creatures, and the screen
            // drew two blank placeholder squares over two empty health bars until it
            // was asked this. Both rects come from the battle layout, which the Ack
            // screen shares.
            const creature::Layout bl = creature::layout(LW, LH, creature::Mode::Menu);
            int sprite_ink = 0;
            for (int yy = bl.theirs.y * SS; yy < (bl.theirs.y + bl.theirs.h) * SS; ++yy)
                for (int xx = bl.theirs.x * SS; xx < (bl.theirs.x + bl.theirs.w) * SS; ++xx) {
                    const std::uint32_t px =
                        buf[static_cast<std::size_t>(yy) * PW + static_cast<std::size_t>(xx)];
                    if (px != gfx::rgb(0x2e, 0x3d, 0x4e)) ++sprite_ink;   // the sky
                }
            CHECK(sprite_ink == 0);
        }

        // ...and Continue puts the game back, without healing the party as a blackout
        // would: a rated match never touched `world_.phase`.
        const int hp_before = scene.world().party.member[0].hp;
        const creature::Layout ack = scene.controls();
        const auto [kx, ky] = centre(ack.ack);
        tap(kx, ky);
        render(idle);
        CHECK(scene.online() == nullptr);
        CHECK(scene.mode() == creature::Mode::Overworld);
        CHECK(scene.world().party.member[0].hp == hp_before);

        // ---- Cancel, by TAPPING it -------------------------------------------
        // Not by calling cancel_online() behind the screen: four chapters of this
        // project shipped a control drawn perfectly and wired to nothing.
        CHECK(scene.start_online(nowhere));
        CHECK(scene.mode() == creature::Mode::Online);
        {
            const creature::Layout q = scene.controls();
            const auto [cx, cy] = centre(q.back);
            tap(cx, cy);
        }
        CHECK(scene.online() == nullptr);
        CHECK(scene.mode() == creature::Mode::Overworld);

        // ---- ...and the BUTTON starts one, a second time ----------------------
        // What is asserted is that the button created a session — that is the button's
        // job. Where the session then gets to belongs to the config it was given, and
        // this one is the real default: a test that also asserted the outcome would be
        // asserting whatever happens to be listening on this machine's port 8080.
        {
            const creature::Layout again = scene.controls();
            CHECK(!again.online.empty());
            const auto [ox, oy] = centre(again.online);
            tap(ox, oy);
            CHECK(scene.online() != nullptr);
            scene.cancel_online();
            CHECK(scene.online() == nullptr);
        }
    }

    clear_file("saves/creatures/slot1.sav");
    if (g_failures == 0) std::printf("test_creatures_scene: all checks passed\n");
    else                 std::printf("test_creatures_scene: %d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
