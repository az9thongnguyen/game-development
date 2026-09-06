// =============================================================================
//  games/creatures/world.hpp  —  the loop around the battle
// =============================================================================
//  `battle.hpp` answers "what happens in a turn". This answers everything a turn is
//  FOR: walking a grid, being ambushed in long grass, throwing a ball, growing a
//  level, evolving, blacking out and waking up somewhere safe — and writing all of
//  that to a save.
//
//  It is pure for the same reason the battle is. Nothing here opens a file, draws a
//  pixel or reads a clock; the map arrives as a `tilemap::Map&` and the scene does
//  the rest. That is what lets `test_creature_world` walk a hundred steps, fight what
//  it meets and check the party grew, with no window anywhere.
//
//  ONE STREAM. The overworld's encounter rolls and the battle's damage rolls come out
//  of the same `World::rng`, advanced only here and in `step`. Two streams would mean
//  a save that restored the battle but not the walk that led to it, and the first
//  time anybody replayed a session it would diverge in the grass.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "engine/tilemap/map2.hpp"
#include "games/creatures/battle.hpp"
#include "games/creatures/defs.hpp"

namespace creature {

// The semantic ids the route's `ground` layer uses. They are named here rather than
// in the theme because whether a tile AMBUSHES YOU is a fact about the world, exactly
// like whether a material is a road (chapter 134). The theme only says what it looks
// like.
inline constexpr int kGrass    = 1;
inline constexpr int kPath     = 2;
inline constexpr int kLongGrass = 3;
inline constexpr int kWater    = 4;

inline constexpr int kMaxBalls        = 20;
inline constexpr int kEncounterPct    = 18;   // per step taken IN long grass
inline constexpr int kSaveVersion     = 1;

enum class Phase : std::uint8_t {
    Overworld = 0,
    Battle,        // a wild creature is out; the player owes an action
    Won,           // ...and it fainted. Waiting to be acknowledged
    Caught,        // ...and it is yours now
    Fled,          // ...and you ran
    Blackout       // your whole party fainted
};

struct World {
    // ---- the overworld ----
    int px = 0, py = 0;          // the player's tile
    int facing = 2;              // 0 N, 1 E, 2 S, 3 W — for the sprite, not the rules
    int home_x = 0, home_y = 0;  // where a blackout puts you back

    // ---- what you have ----
    Party party;
    int   balls = 10;
    int   wins  = 0;
    int   caught = 0;

    // ---- the fight ----
    Phase  phase = Phase::Overworld;
    Battle battle;
    int    wild_species = 0;
    int    wild_level   = 0;
    std::vector<Event> log;      // events from the last turn, for the scene to narrate

    // ---- the one stream ----
    std::uint64_t rng = 1;
};

// A fresh game: one starter at level 5, ten balls, standing at (x, y) which also
// becomes home. `starter` is a species id.
World new_game(const Dex& d, int starter, int x, int y, std::uint64_t seed);

// ---- the overworld --------------------------------------------------------------

struct WalkResult {
    bool moved     = false;
    bool blocked   = false;
    bool encounter = false;      // a battle started; `phase` is now Battle
};

// One tile in one direction. Refuses to leave the map, refuses a `collide` cell, and
// does nothing at all while a battle is up — the phase is the only gate, so a caller
// that forgets to check cannot walk out of a fight.
//
// The encounter roll happens on the tile you ARRIVE at, and only in long grass. It is
// rolled every step rather than on a counter, because a counter makes the third step
// after an encounter safe in a way a player learns and then exploits.
WalkResult walk(const Dex& d, World& w, const tilemap::Map& m, int dx, int dy);

// ---- the fight ------------------------------------------------------------------

// Start one deliberately (a test, or a scripted meeting later).
void begin_battle(const Dex& d, World& w, int species, int level);

// The player's action for this turn; the wild side is chosen by `choose`. Resolves
// one `step`, drains the events into `w.log`, and settles the aftermath: experience,
// levels, evolution, blackout.
void battle_turn(const Dex& d, World& w, Action player);

// Throw a ball. Costs a ball whether or not it works, advances the same stream, and
// on failure the wild creature still gets its turn — a free look at whether a throw
// would land is the one thing that would make balls meaningless.
bool throw_ball(const Dex& d, World& w);

// Leave the fight. `Won`, `Caught` and `Fled` all return to the overworld; `Blackout`
// heals the party and puts the player home. Called when the player acknowledges the
// end of a battle, which is why it is separate from `battle_turn`.
void end_battle(const Dex& d, World& w);

// ---- growing --------------------------------------------------------------------

// Experience needed to leave level L. `L*L*2` — flat enough that the first few levels
// come quickly and steep enough that a level-40 party is not an afternoon.
[[nodiscard]] int exp_to_next(int level);

struct Growth {
    int levels_gained = 0;
    int evolved_from  = 0;      // species ids; 0 = it did not evolve
    int evolved_to    = 0;
    std::vector<int> learned;   // move indices picked up on the way
};

// Award experience to the active creature and apply everything that follows. Public
// because it is the most interesting pure function here and deserves its own test:
// stats have to grow WITHOUT healing (a level-up that refilled HP would end every
// fight), and an evolution has to keep the damage already taken.
Growth award_exp(const Dex& d, World& w, int amount);

// ---- the save -------------------------------------------------------------------

std::string to_text(const World& w);
bool        from_text(const Dex& d, const std::string& text, World& out);

} // namespace creature
