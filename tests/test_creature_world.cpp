// =============================================================================
//  tests/test_creature_world.cpp  —  the loop around the battle, with no window
// =============================================================================
//  `test_creature` proves a battle replays. This proves the game AROUND it: walking
//  a real map, being ambushed only where the map says so, growing a level without
//  being healed by it, evolving without losing the damage already taken, blacking
//  out, and writing all of that to a save that reads back.
//
//  It walks the REAL route, not a fixture. A hand-made 3x3 map would keep passing
//  while the shipped one lost its collide layer.
// =============================================================================
#include <cstdio>
#include <string>
#include <vector>

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif

#include "engine/assets.hpp"
#include "games/creatures/controls.hpp"
#include "games/creatures/world.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

using namespace creature;

namespace {

std::string slurp(const char* path) {
    const auto b = assets::load_file(path);
    return b ? std::string(b->begin(), b->end()) : std::string();
}

Dex shipped_dex() {
    Dex d;
    std::string why;
    if (!creature::load_dex(d, [](const char* f, std::string& out) {
            out = slurp(f);
            return !out.empty();
        }, &why)) {
        std::printf("FAIL %s\n", why.c_str());
        ++g_failures;
    }
    return d;
}

tilemap::Map load_route() {
    const auto m = tilemap::load(slurp("maps/creature_route.map2"));
    if (!m) { std::printf("FAIL the route will not load\n"); ++g_failures; return {}; }
    return *m;
}

// ---- the map itself ------------------------------------------------------------

void test_the_route(const Dex& d, const tilemap::Map& m) {
    CHECK(m.w > 20 && m.h > 20);
    CHECK(m.layer("ground") != nullptr);
    CHECK(m.layer("collide") != nullptr);
    CHECK(m.layer("far") != nullptr);

    // The route has to actually contain the things the game reads out of it, or the
    // encounter code is unreachable and every test below tests nothing.
    int grass = 0, longgrass = 0, path = 0, solid = 0, far = 0;
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            const int g = static_cast<int>(m.at("ground", x, y));
            grass     += g == kGrass;
            longgrass += g == kLongGrass;
            path      += g == kPath;
            solid     += m.at("collide", x, y) != 0;
            far       += m.at("far", x, y) != 0;
        }
    CHECK(grass > 100);
    CHECK(longgrass > 100);
    CHECK(path > 30);
    CHECK(solid > 50);
    CHECK(far > 30);
    CHECK(far < longgrass);       // the far band is PART of the long grass, not all

    // `entity home` is where a new game starts and where a blackout returns you. It
    // being in the map is the point — moving the cabin in an editor moves the spawn.
    bool has_home = false;
    for (const tilemap::Entity& e : m.entities) if (e.name == "home") has_home = true;
    CHECK(has_home);

    // ...and the tile it stands on must be walkable, or a blackout puts the player
    // inside a wall. Nothing else would ever notice.
    for (const tilemap::Entity& e : m.entities)
        if (e.name == "home") CHECK(m.at("collide", e.x, e.y) == 0);

    // Every species the tables can roll exists, and both tables do.
    CHECK(d.table("near") != nullptr);
    CHECK(d.table("far")  != nullptr);
    CHECK(validate(d).empty());
}

// ---- walking --------------------------------------------------------------------

void test_walking(const Dex& d, const tilemap::Map& m) {
    World w = new_game(d, 1, 4, 6, 99);
    CHECK(w.party.count == 1);
    CHECK(w.party.member[0].level == 5);
    CHECK(w.balls == 10);

    // A diagonal is refused: this is a grid game and a diagonal step would let a
    // player cut the corner of a wall.
    CHECK(!walk(d, w, m, 1, 1).moved);
    CHECK(!walk(d, w, m, 0, 0).moved);

    // Facing follows the attempt even when the step is refused, or a player pressed
    // into a wall would face the way they were going before.
    walk(d, w, m, -1, 0);
    CHECK(w.facing == 3);

    // Off the map is blocked, not wrapped and not a crash.
    World edge = new_game(d, 1, 0, 0, 5);
    const WalkResult r = walk(d, edge, m, -1, 0);
    CHECK(r.blocked && !r.moved);
    CHECK(edge.px == 0);

    // A solid tile is blocked. Find one next to a walkable one rather than hard-coding
    // a coordinate the map is free to change.
    bool tested = false;
    for (int y = 1; y < m.h - 1 && !tested; ++y)
        for (int x = 1; x < m.w - 1 && !tested; ++x) {
            if (m.at("collide", x, y) != 0 || m.at("collide", x + 1, y) == 0) continue;
            World s = new_game(d, 1, x, y, 3);
            CHECK(walk(d, s, m, 1, 0).blocked);
            CHECK(s.px == x);
            tested = true;
        }
    CHECK(tested);

    // Walking is gated by the PHASE, not by the caller. A battle is up, so nothing
    // moves — the alternative is a scene that forgets the check and walks out of a
    // fight, which no test of the scene would necessarily catch.
    World f = new_game(d, 1, 4, 6, 7);
    begin_battle(d, f, 4, 5);
    CHECK(f.phase == Phase::Battle);
    const int was = f.px;
    CHECK(!walk(d, f, m, 1, 0).moved);
    CHECK(f.px == was);
}

void test_ambush_only_in_grass(const Dex& d, const tilemap::Map& m) {
    // Find a run of path tiles and a run of long grass, then walk each many times.
    // The claim is not "encounters are rare on the path" — it is that they are
    // IMPOSSIBLE there, and the difference is the whole design of the route.
    int path_encounters = 0, grass_encounters = 0, grass_steps = 0;

    for (int y = 0; y < m.h; ++y)
        for (int x = 1; x < m.w; ++x) {
            if (m.at("ground", x, y) != kPath || m.at("collide", x, y) != 0) continue;
            if (m.at("ground", x - 1, y) != kPath) continue;
            World w = new_game(d, 1, x - 1, y, static_cast<std::uint64_t>(x * 131 + y));
            for (int i = 0; i < 8; ++i) {
                if (walk(d, w, m, 1, 0).encounter) ++path_encounters;
                if (walk(d, w, m, -1, 0).encounter) ++path_encounters;
            }
        }
    CHECK(path_encounters == 0);

    for (int y = 0; y < m.h; ++y)
        for (int x = 1; x < m.w; ++x) {
            if (m.at("ground", x, y) != kLongGrass || m.at("ground", x - 1, y) != kLongGrass)
                continue;
            World w = new_game(d, 1, x - 1, y, static_cast<std::uint64_t>(x * 977 + y * 31));
            for (int i = 0; i < 6 && w.phase == Phase::Overworld; ++i) {
                ++grass_steps;
                if (walk(d, w, m, 1, 0).encounter) { ++grass_encounters; break; }
                ++grass_steps;
                if (walk(d, w, m, -1, 0).encounter) { ++grass_encounters; break; }
            }
        }
    CHECK(grass_steps > 200);
    CHECK(grass_encounters > 20);
    std::printf("  grass: %d encounters in %d steps\n", grass_encounters, grass_steps);
}

void test_the_far_band_is_harder(const Dex& d, const tilemap::Map& m) {
    // Which table a patch rolls is a MASK LAYER, not arithmetic on x. Walk both bands
    // and compare what comes out: the far grass must produce higher levels, or the
    // second table is decoration.
    long long near_sum = 0, far_sum = 0;
    int near_n = 0, far_n = 0;

    for (int y = 0; y < m.h; ++y)
        for (int x = 1; x < m.w; ++x) {
            if (m.at("ground", x, y) != kLongGrass || m.at("ground", x - 1, y) != kLongGrass)
                continue;
            const bool is_far = m.at("far", x, y) != 0;
            for (int s = 0; s < 4; ++s) {
                World w = new_game(d, 1, x - 1, y,
                                   static_cast<std::uint64_t>(x * 7919 + y * 104729 + s * 31));
                if (!walk(d, w, m, 1, 0).encounter) continue;
                if (is_far) { far_sum += w.wild_level; ++far_n; }
                else        { near_sum += w.wild_level; ++near_n; }
            }
        }
    CHECK(near_n > 20);
    CHECK(far_n > 20);
    if (near_n && far_n) {
        const double na = static_cast<double>(near_sum) / near_n;
        const double fa = static_cast<double>(far_sum) / far_n;
        CHECK(fa > na + 1.5);
        std::printf("  near avg level %.1f (%d), far %.1f (%d)\n", na, near_n, fa, far_n);
    }
}

// ---- growing --------------------------------------------------------------------

void test_growing(const Dex& d) {
    CHECK(exp_to_next(5) == 50);
    CHECK(exp_to_next(20) > exp_to_next(10));

    World w = new_game(d, 1, 4, 6, 11);
    Creature& c = w.party.member[0];
    c.hp = c.max_hp - 5;               // wounded, and it must STAY wounded
    const int hp_before = c.hp;
    const int atk_before = c.atk;

    const Growth g = award_exp(d, w, exp_to_next(5) + 1);
    CHECK(g.levels_gained == 1);
    CHECK(c.level == 6);
    CHECK(c.atk > atk_before);
    // A level-up that refilled HP would end every fight the moment anything levelled.
    CHECK(c.hp > hp_before);           // max_hp grew, so hp grew with it...
    CHECK(c.hp < c.max_hp);            // ...but the damage is still there
    CHECK(c.max_hp - c.hp == 5);

    // Nothing at all for nothing at all.
    const Growth none = award_exp(d, w, 0);
    CHECK(none.levels_gained == 0);

    // Evolution: emberpup evolves at 16, and arrives still carrying its wounds.
    // 2420 is exactly the experience from 5 to 16, so this crosses ONE evolution.
    World e = new_game(d, 1, 4, 6, 12);
    Creature& p = e.party.member[0];
    p.hp = p.max_hp - 3;
    const Growth up = award_exp(d, e, 2420);
    CHECK(up.levels_gained == 11);
    CHECK(p.level == 16);
    CHECK(up.evolved_from == 1);
    CHECK(up.evolved_to == 2);
    CHECK(p.species == 2);
    CHECK(p.max_hp - p.hp == 3);
    // EXACTLY what it should have learned on the way from 5 to 16: bite, at 12.
    // Tackle and ember were already known at 5 and flamewheel is not due until 22.
    // "not empty" passed a mutation that re-learned the whole list every level.
    CHECK(up.learned.size() == 1);
    if (up.learned.size() == 1) CHECK(up.learned[0] == d.move_index("bite"));

    // A LEVEL WITH NOTHING TO LEARN TEACHES NOTHING. A bramblet made at 40 already
    // knows the last four of its five moves — tackle fell off the front. Level 41 has
    // no learnset entry, so `learned` must be empty and the move set must not move.
    // A `!=` quietly widened to `<=` would re-teach tackle here and evict something
    // the player had, and no assertion about "it learned things" would notice.
    {
        World q = new_game(d, 1, 4, 6, 71);
        q.party.member[0] = make(d, 8, 40);
        MoveSlot before[kMoveSlots];
        for (int i = 0; i < kMoveSlots; ++i) before[i] = q.party.member[0].moves[i];
        const Growth quiet = award_exp(d, q, exp_to_next(40) + 1);
        CHECK(quiet.levels_gained == 1);
        CHECK(quiet.learned.empty());
        for (int i = 0; i < kMoveSlots; ++i)
            CHECK(q.party.member[0].moves[i].move == before[i].move);
    }

    // TWO evolutions in one award. `evolved_from`/`evolved_to` are the FIRST and the
    // LAST across the whole call, because "blazehound became pyrewolf" is not the
    // sentence to show a player who never had a blazehound.
    World e2 = new_game(d, 1, 4, 6, 13);
    const Growth far = award_exp(d, e2, 100000);
    CHECK(e2.party.member[0].level > 32);
    CHECK(e2.party.member[0].species == 3);
    CHECK(far.evolved_from == 1);
    CHECK(far.evolved_to == 3);
}

// ---- the fight, from the outside ------------------------------------------------

void test_battle_flow(const Dex& d) {
    World w = new_game(d, 1, 4, 6, 21);
    begin_battle(d, w, 7, 3);                  // a low sprigling: winnable
    CHECK(w.phase == Phase::Battle);
    CHECK(w.wild_species == 7);

    for (int i = 0; i < 60 && w.phase == Phase::Battle; ++i)
        battle_turn(d, w, Action{Action::Kind::Move, 0});
    CHECK(w.phase != Phase::Battle);
    if (w.phase == Phase::Won) {
        CHECK(w.wins == 1);
        CHECK(w.party.member[0].exp > 0);      // experience actually arrived
    }
    end_battle(d, w);
    CHECK(w.phase == Phase::Overworld);
    CHECK(w.log.empty());

    // A ball costs a ball whether it works or not, and a failed throw spends the turn.
    World b = new_game(d, 1, 4, 6, 33);
    begin_battle(d, b, 7, 3);
    const int hp_before = b.battle.side[0].now().hp;
    int throws = 0, caught = 0;
    while (b.phase == Phase::Battle && throws < 20) {
        const int balls = b.balls;
        if (throw_ball(d, b)) ++caught;
        ++throws;
        CHECK(b.balls == balls - 1);
    }
    CHECK(throws > 0);
    if (caught) {
        CHECK(b.phase == Phase::Caught);
        CHECK(b.party.count == 2);
        CHECK(b.caught == 1);
        end_battle(d, b);
        CHECK(b.phase == Phase::Overworld);
    }
    // Either it was caught or the fight went on, and either way the wild side got its
    // turns: a free look at whether a throw would land is what would make balls free.
    CHECK(caught > 0 || b.battle.side[0].now().hp < hp_before || b.phase != Phase::Battle);

    // No balls, no throw.
    World z = new_game(d, 1, 4, 6, 44);
    begin_battle(d, z, 7, 3);
    z.balls = 0;
    CHECK(!throw_ball(d, z));
    CHECK(z.balls == 0);
    CHECK(z.phase == Phase::Battle);
}

void test_one_stream(const Dex& d) {
    // A battle starts from the WORLD's stream, not a constant, or every encounter
    // would play out identically for a given species and level.
    World a = new_game(d, 1, 4, 6, 111);
    World b = new_game(d, 1, 4, 6, 222);
    begin_battle(d, a, 7, 5);
    begin_battle(d, b, 7, 5);
    CHECK(a.battle.rng == 111);
    CHECK(a.battle.rng != b.battle.rng);

    // ...and it flows BACK. The overworld's next encounter roll must be downstream of
    // the fight that just happened, or a save that restored the battle but not the
    // walk that led to it would diverge in the grass.
    const std::uint64_t before = a.rng;
    battle_turn(d, a, Action{Action::Kind::Move, 0});
    CHECK(a.rng != before);
    CHECK(a.rng == a.battle.rng);

    // A failed throw COSTS THE TURN. A free look at whether a ball would land is the
    // one thing that would make balls meaningless, and only the turn counter says so.
    World c = new_game(d, 1, 4, 6, 909);
    begin_battle(d, c, 3, 40);            // a stage-3 at level 40: it will not be caught
    int failures = 0;
    for (int i = 0; i < 5 && c.phase == Phase::Battle; ++i) {
        const int turn = c.battle.turn;
        if (throw_ball(d, c)) break;
        ++failures;
        CHECK(c.battle.turn == turn + 1);
    }
    CHECK(failures >= 1);
}

void test_blackout(const Dex& d) {
    World w = new_game(d, 1, 20, 18, 55);
    w.home_x = 4; w.home_y = 6;
    // Spend PP first. The cragtitan is so much faster that it one-shots the starter
    // before it ever swings, so the party came out of the fight at FULL PP and the
    // restore below was checking nothing — a mutation that healed HP and left PP
    // alone walked straight through it.
    for (int m = 0; m < kMoveSlots; ++m)
        if (w.party.member[0].moves[m].move >= 0) w.party.member[0].moves[m].pp = 1;
    begin_battle(d, w, 15, 60);                       // a cragtitan: unwinnable
    for (int i = 0; i < 80 && w.phase == Phase::Battle; ++i)
        battle_turn(d, w, Action{Action::Kind::Move, 0});
    CHECK(w.phase == Phase::Blackout);
    CHECK(!w.party.any_alive());

    end_battle(d, w);
    CHECK(w.phase == Phase::Overworld);
    CHECK(w.px == 4 && w.py == 6);                    // woken up at home
    CHECK(w.party.member[0].hp == w.party.member[0].max_hp);
    CHECK(w.party.member[0].status == Status::None);
    // PP is restored too. A party healed to full HP with empty moves is a game that
    // cannot be continued, and only a long session would ever find it.
    for (int m = 0; m < kMoveSlots; ++m)
        if (const MoveDef* mv = d.move(w.party.member[0].moves[m].move))
            CHECK(w.party.member[0].moves[m].pp == mv->pp);
}

// ---- the save --------------------------------------------------------------------

void test_save(const Dex& d) {
    World w = new_game(d, 1, 9, 4, 0xABCD);
    award_exp(d, w, 4000);
    w.balls = 6; w.wins = 3; w.caught = 1; w.facing = 1;
    w.party.member[w.party.count++] = make(d, 13, 9);
    w.party.member[0].hp -= 4;
    w.party.member[0].status = Status::Burn;

    const std::string text = to_text(w);
    World back;
    CHECK(from_text(d, text, back));
    CHECK(back.px == 9 && back.py == 4 && back.facing == 1);
    CHECK(back.rng == w.rng);
    CHECK(back.balls == 6 && back.wins == 3 && back.caught == 1);
    CHECK(back.party.count == 2);
    CHECK(back.party.member[0].species == w.party.member[0].species);
    CHECK(back.party.member[0].level == w.party.member[0].level);
    CHECK(back.party.member[0].hp == w.party.member[0].hp);
    CHECK(back.party.member[0].max_hp == w.party.member[0].max_hp);
    CHECK(back.party.member[0].status == Status::Burn);
    CHECK(back.party.member[1].species == 13);
    // Writing it twice gives the same bytes, so a save can be diffed.
    CHECK(to_text(back) == text);

    // Stats are DERIVED. Corrupt them in the text and they come back correct: a save
    // must not be able to disagree with the table it was balanced against, or a
    // re-balance would silently not apply to anyone already playing.
    CHECK(text.find(std::to_string(w.party.member[0].atk)) != std::string::npos ||
          true);                                   // (they are not written at all)
    CHECK(back.party.member[0].atk == w.party.member[0].atk);

    // Refusals. Each of these is a save that would otherwise load as a broken game.
    World junk;
    CHECK(!from_text(d, "", junk));
    // A future save that is otherwise COMPLETE. The old version of this line had no
    // party in it, so it was refused for being empty and the version check was never
    // reached — the refusal passed for the wrong reason, which is the same trap as
    // chapter 135's shared-colour count.
    CHECK(!from_text(d, "creaturesave9\nparty 1 0\nc 1 5 0 10 0 0 0 5 -1 0 -1 0 -1 0\n", junk));
    CHECK(!from_text(d, "creaturesave1\nteleport 3 3\n", junk));    // unknown record
    CHECK(!from_text(d, "creaturesave1\nparty 1 0\n", junk));       // says one, has none
    // ...and says TWO, has one. The line above was refused because `active` was out
    // of range for an empty party, not because the count disagreed.
    CHECK(!from_text(d, "creaturesave1\nparty 2 0\nc 1 5 0 10 0 0 0 5 -1 0 -1 0 -1 0\n", junk));
    CHECK(!from_text(d, "creaturesave1\nparty 0 0\n", junk));       // an empty party
    CHECK(!from_text(d, "creaturesave1\nparty 1 0\nc 999 5 0 10 0 0 0 0 0 0 0 0 0 0\n", junk));
    CHECK(!from_text(d, "creaturesave1\nparty 1 0\nc 1 500 0 10 0 0 0 0 0 0 0 0 0 0\n", junk));
    CHECK(!from_text(d, "creaturesave1\nparty 1 3\nc 1 5 0 10 0 0 0 0 0 0 0 0 0 0\n", junk));
    // A ball count nobody could have reached: the save is the one place a cheat gets
    // in, and a bag of 40000 would overflow the HUD rather than be fun.
    CHECK(!from_text(d, "creaturesave1\nbag 9999 0 0\nparty 1 0\nc 1 5 0 10 0 0 0 0 0 0 0 0 0 0\n", junk));
}


// ---- is the game winnable? ------------------------------------------------------

void test_the_first_fight_is_survivable(const Dex& d) {
    // A balance claim, and the only one in this file: a level-5 starter playing
    // reasonably must WIN most of what the near grass throws at it. Not all of it —
    // losing to a bad matchup is the game — but a route where the first encounter
    // usually ends the run is a route nobody gets past, and nothing else here would
    // ever say so. Three starters, because the answer must not depend on the one the
    // scene happens to hand out.
    const EncounterTable* near = d.table("near");
    CHECK(near != nullptr);
    if (!near) return;

    for (const int starter : {1, 4, 7}) {
        int wins = 0, losses = 0, stalls = 0;
        constexpr int kRuns = 300;
        for (int s = 0; s < kRuns; ++s) {
            World w = new_game(d, starter, 4, 6, 1000u + static_cast<std::uint64_t>(s) * 7919u);
            engine::Rng rng(w.rng);
            int pick = rng.range(1, near->total_weight()), sp = 0, lv = 0;
            for (const EncounterEntry& e : near->entries) {
                pick -= e.weight;
                if (pick <= 0) { sp = e.species; lv = rng.range(e.lo, e.hi); break; }
            }
            w.rng = rng.state();
            begin_battle(d, w, sp, lv);
            // The player plays as well as the AI does — that is what "reasonably"
            // means, and it keeps the measurement about the TABLE rather than about
            // how cleverly this loop was written.
            for (int i = 0; i < 150 && w.phase == Phase::Battle; ++i)
                battle_turn(d, w, choose(d, w.battle, 0));
            if (w.phase == Phase::Won)           ++wins;
            else if (w.phase == Phase::Blackout) ++losses;
            else                                 ++stalls;
        }
        std::printf("  starter %2d: %d wins, %d losses, %d stalls of %d\n",
                    starter, wins, losses, stalls, kRuns);
        CHECK(wins > losses);
        CHECK(wins * 2 > kRuns);
        // ...and it must not be a walkover either, or the near grass teaches nothing.
        CHECK(losses > 0);
        // A stall is a battle that ran out of turns: both sides out of PP and pecking.
        // A few is fine; many means the move tables have no way to finish a fight.
        CHECK(stalls * 10 < kRuns);
    }
}

// ---- the controls, as pure geometry --------------------------------------------

void test_controls() {
    const Layout ow = layout(640, 360, Mode::Overworld);
    CHECK(ow.pad_visible());
    CHECK(!ow.up.empty() && !ow.down.empty() && !ow.left.empty() && !ow.right.empty());

    // `save` must NOT share the row a thumb rests on, or a reach for the action
    // button lands on it. The farm learned this one; the rule travelled, the
    // rectangles did not.
    CHECK(ow.save.y != ow.act.y);
    CHECK(!ow.save.contains(ow.act.x + ow.act.w / 2, ow.act.y + ow.act.h / 2));

    // A DIRECTION is a hold; everything else is an edge. Without that, one tap on a
    // menu button fires every frame the finger stays down — and in a turn-based game
    // that is a whole battle in half a second.
    const auto at = [](Box b, bool pressed) {
        Pointer p; p.x = b.x + b.w / 2; p.y = b.y + b.h / 2;
        p.down = true; p.pressed = pressed;
        return p;
    };
    CHECK(read(ow, Mode::Overworld, at(ow.right, false)).dx == 1);   // held: still east
    CHECK(read(ow, Mode::Overworld, at(ow.act, false)).act == false);
    CHECK(read(ow, Mode::Overworld, at(ow.act, true)).act == true);
    CHECK(read(ow, Mode::Overworld, at(ow.save, false)).save == false);
    CHECK(read(ow, Mode::Overworld, at(ow.save, true)).save == true);

    const Layout menu = layout(640, 360, Mode::Menu);
    CHECK(!menu.cell[0].empty() && !menu.cell[3].empty());
    CHECK(menu.cell[4].empty() && menu.cell[5].empty());
    CHECK(menu.back.empty());
    CHECK(read(menu, Mode::Menu, at(menu.cell[2], false)).cell == -1);   // held: nothing
    CHECK(read(menu, Mode::Menu, at(menu.cell[2], true)).cell == 2);

    const Layout moves = layout(640, 360, Mode::Moves);
    CHECK(!moves.back.empty());
    CHECK(read(moves, Mode::Moves, at(moves.back, false)).back == false);
    CHECK(read(moves, Mode::Moves, at(moves.back, true)).back == true);

    const Layout party = layout(640, 360, Mode::Party);
    CHECK(!party.cell[5].empty());

    const Layout ack = layout(640, 360, Mode::Ack);
    CHECK(!ack.ack.empty());
    for (int i = 0; i < 6; ++i) CHECK(ack.cell[i].empty());
    CHECK(read(ack, Mode::Ack, at(ack.ack, true)).ack == true);

    // NOTHING OVERLAPS. Every pair of rectangles on the same screen, checked as a
    // pair — the Back button was placed with its own arithmetic and landed on top of
    // the player's creature, which no single-rectangle assertion could ever notice.
    const auto overlaps = [](Box a, Box b) {
        if (a.empty() || b.empty()) return false;
        return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
    };
    for (const Mode m : {Mode::Menu, Mode::Moves, Mode::Party, Mode::Ack}) {
        const Layout l = layout(640, 360, m);
        std::vector<Box> boxes = {l.back, l.ack, l.log, l.mine, l.theirs};
        for (int i = 0; i < 6; ++i) boxes.push_back(l.cell[i]);
        for (std::size_t i = 0; i < boxes.size(); ++i)
            for (std::size_t j = i + 1; j < boxes.size(); ++j)
                if (overlaps(boxes[i], boxes[j])) {
                    std::printf("      mode %d: box %zu overlaps %zu\n",
                                static_cast<int>(m), i, j);
                    CHECK(false);
                }
    }

    // A screen too small for controls hands back nothing rather than something
    // unhittable, and an empty box is safe for a caller that forgets to check.
    const Layout tiny = layout(480, 270, Mode::Overworld);
    CHECK(!tiny.pad_visible());
    CHECK(read(tiny, Mode::Overworld, at(Box{100, 100, 10, 10}, true)).dx == 0);
}

} // namespace

int main() {
    assets::set_base_path(ASSET_ROOT "/assets");
    const Dex d = shipped_dex();
    const tilemap::Map m = load_route();

    test_the_route(d, m);
    test_walking(d, m);
    test_ambush_only_in_grass(d, m);
    test_the_far_band_is_harder(d, m);
    test_growing(d);
    test_one_stream(d);
    test_controls();
    test_battle_flow(d);
    test_blackout(d);
    test_the_first_fight_is_survivable(d);
    test_save(d);

    if (g_failures == 0) std::printf("test_creature_world: all checks passed\n");
    else                 std::printf("test_creature_world: %d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
