// =============================================================================
//  games/creatures/battle.hpp  —  a turn-based battle, as INTEGER ARITHMETIC
// =============================================================================
//  Everything in this file exists to make one sentence true:
//
//      the same starting state and the same list of actions produce the same
//      battle, on every machine, forever.
//
//  That is not a nicety. It is what makes a replay a fact rather than a video, what
//  lets two players fight over a network by exchanging four bytes a turn instead of
//  a world state, and what lets a desync be DETECTED (both sides hash after every
//  turn) instead of quietly deciding the winner.
//
//  Three rules protect it, and each one is a thing this file refuses to do:
//
//   1. NO FLOATING POINT anywhere in the resolution path. Effectiveness is a
//      percent, STAB is *150/100, the damage roll is *85..100/100. A `float` here
//      would be right on both machines almost always, and the word "almost" is the
//      entire problem.
//   2. THE RNG IS STATE, NOT A SERVICE. `Battle::rng` is a field, it is hashed, and
//      it is advanced only from inside `step`. A battle that reached for a global
//      generator would replay correctly exactly until something else drew a number.
//   3. RESOLUTION ORDER IS DERIVED FROM STATE ONLY — priority, then speed, then one
//      coin flip from the battle's own stream. Never "side 0 first", which is the
//      cheap version that works until the two sides are two different computers.
//
//  `step` returns EVENTS rather than text. An event is `{kind, side, a, b}` with no
//  strings in it, because the core must stay compilable into a headless test and
//  because the same battle has to be narrated in one language on screen and in a log
//  line somewhere else. Sentences are the caller's job.
//
//  PURE: no I/O, no renderer, no clock.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "engine/rand.hpp"
#include "games/creatures/defs.hpp"

namespace creature {

inline constexpr int kPartySize = 6;
inline constexpr int kMoveSlots = 4;

enum class Status : std::uint8_t { None = 0, Burn, Paralyze, Sleep };

struct MoveSlot {
    int move = -1;    // index into Dex::moves; -1 = the slot is empty
    int pp   = 0;
};

struct Creature {
    int      species = 0;      // 0 = an empty party slot
    int      level   = 5;
    int      exp     = 0;      // toward the NEXT level; see creatures/world.hpp
    int      hp      = 0;
    int      max_hp  = 0;
    int      atk = 0, def = 0, spd = 0;
    Status   status  = Status::None;
    int      sleep   = 0;      // turns of sleep left
    MoveSlot moves[kMoveSlots];

    [[nodiscard]] bool alive() const { return species != 0 && hp > 0; }
};

struct Party {
    Creature member[kPartySize];
    int      count  = 0;
    int      active = 0;

    [[nodiscard]] const Creature& now() const { return member[active]; }
    [[nodiscard]] Creature&       now()       { return member[active]; }
    [[nodiscard]] bool any_alive() const;
    [[nodiscard]] int  first_alive(int except = -1) const;   // -1 when there is none
};

struct Action {
    enum class Kind : std::uint8_t { Move = 0, Switch, Run, Ball };
    Kind kind  = Kind::Move;
    int  index = 0;      // move slot 0..3, party slot 0..5, or a ball's bonus percent
};

// No strings, and no pointers into the Dex: an event has to survive being written to
// a log, sent over a socket and read back by a build that loaded a different file.
//
// New kinds go on the END. Nothing writes an Event to a file today, but the reason
// they are numbers rather than strings is so that one day something can, and a format
// whose meanings shuffle when a kind is added is not a format.
struct Event {
    enum class Kind : std::uint8_t {
        Used = 0,       // a=move index
        Missed,         // a=move index
        Damage,         // a=hp lost, b=effectiveness percent
        Fainted,
        Switched,       // a=party slot switched IN
        Inflicted,      // a=Status
        StatusHurt,     // a=hp lost, b=Status
        Immobilised,    // a=Status  (asleep, or paralysis held it)
        WokeUp,
        NoPP,           // a=move slot
        Fled,
        Win,            // side = winner
        Ball            // a=1 it stuck, a=0 it broke free.  APPENDED, not inserted:
                        // see the note above `Kind` about what these numbers are
    };
    Kind kind = Kind::Used;
    int  side = 0;      // whose creature the event is ABOUT
    int  a = 0, b = 0;
};

struct Battle {
    Party         side[2];
    std::uint64_t rng    = 0;
    int           turn   = 0;
    bool          over   = false;
    int           winner = -1;    // -1 while running or on a draw
    bool          fled   = false;
    bool          caught = false;  // it ended because somebody's ball stuck
};

// ---- building a combatant ------------------------------------------------------

// Stats from base stats and level, integer, the classic shape. A creature made this
// way twice is byte-identical, which is what lets a replay carry a species id and a
// level instead of a stat block.
Creature make(const Dex& d, int species_id, int level);

// The party a wild encounter or a trainer fields.
Party make_party(const Dex& d, const std::vector<std::pair<int, int>>& id_level);

// ---- the turn ------------------------------------------------------------------

// Resolve one turn. Appends to `out` when it is non-null. Does nothing once
// `b.over` is set, so a caller that keeps stepping past the end cannot corrupt a
// finished battle into a different winner.
void step(const Dex& d, Battle& b, Action a0, Action a1, std::vector<Event>* out = nullptr);

// FNV-1a over every field that can affect a future turn, fed one at a time — NOT a
// memcpy of the struct, because struct padding is uninitialised and would make the
// hash differ between two machines that agree perfectly about the battle.
std::uint64_t hash(const Battle& b);

// ---- replay --------------------------------------------------------------------
//
// In `replay.hpp`, since chapter 138. It lives next door rather than here because a
// recording turned out to need two things this file has no opinion about: which
// TABLES it was played under, and a serialised form. `hash` is the whole interface
// between them.

// ---- the opponent --------------------------------------------------------------

// Deterministic, and deliberately does NOT touch `b.rng`: an AI that drew from the
// battle stream would make the stream depend on who was choosing, and a replay
// recorded against the AI would then not reproduce under a human making the same
// choices. It picks the highest expected damage, and switches out at low HP when
// the bench has a better matchup.
Action choose(const Dex& d, const Battle& b, int side);

// ---- catching ------------------------------------------------------------------

// A ball is thrown by passing `Action{Kind::Ball, bonus}` to `step`, and that is how
// the GAME must throw one: a verb resolved outside the turn cannot appear in the list
// of actions a replay is made of, and until chapter 138 the one verb this genre is
// named after was exactly that — resolved beside `step`, invisible to a recording.
//
// This is the same roll, bracketed on its own so the catch MATH can be measured
// without a battle happening around it. `test_creature` pins the two against each
// other from one seed, because two doors into one calculation is a promise, and a
// promise nobody checks is already broken.
bool try_catch(const Dex& d, Battle& b, int side, int ball_bonus = 100);

} // namespace creature
