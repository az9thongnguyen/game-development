// =============================================================================
//  games/creatures/defs.hpp  —  what a type, a move and a species ARE, as data
// =============================================================================
//  Same rule as the farm's crops (chapter 113): the balance pass is the work, and
//  the work must not need a compiler. What is different here is how much of the
//  game is balance. A farm has six numbers per crop; a creature game is a
//  6x6 effectiveness table, seventeen moves and eighteen species, and every one of
//  those numbers exists to be argued with.
//
//      types.def     type fire
//                    eff fire grass 200        # attacker, defender, percent
//      moves.def     move ember type=fire power=40 acc=100 pp=25 effect=burn:10
//      species.def   species 1 emberpup type=fire hp=39 atk=52 def=43 spd=65 \
//                            catch=190 evolve=2@16 moves=1:tackle,5:ember sprite=...
//
//  THE TABLE IS EXCEPTIONS ONLY. Thirty-six numbers written out is thirty-six
//  chances to put a 2 where a 0.5 goes, and a reader cannot tell a deliberate 100
//  from a forgotten one. `eff` lines name only what is not neutral, so the file
//  reads as the design ("fire beats grass") rather than as a matrix.
//
//  Strictness is split the same way it is everywhere in this project: an unknown
//  KEY is ignored (a later field must be additive), an unknown VALUE is an error. A
//  move whose `type=` names a type nobody declared is a typo that would otherwise
//  become a silently neutral matchup — the exact bug that makes a balance pass
//  chase a number that was never being read.
//
//  PURE: text in, structs out. No I/O.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace creature {

// Effectiveness is stored in PERCENT so the whole battle stays integer: 50, 100,
// 200. A float multiplier is the single easiest way to make two machines disagree
// about who won.
inline constexpr int kNeutral = 100;

struct TypeChart {
    std::vector<std::string> names;
    std::vector<int>         eff;      // names.size()^2, row = attacker

    [[nodiscard]] int index(const std::string& name) const;
    [[nodiscard]] int multiplier(int attacker, int defender) const;   // percent
};

enum class Effect : std::uint8_t { None = 0, Burn, Paralyze, Sleep };

struct MoveDef {
    std::string name;
    int    type   = 0;
    int    power  = 40;      // 0 = a status move: it never rolls damage
    int    acc    = 100;
    int    pp     = 25;
    int    priority = 0;     // resolved before speed; one move has +1 so the rule
                             // has a consumer instead of being a promise
    Effect effect = Effect::None;
    int    effect_chance = 0;  // percent; 100 for a status move that always lands
};

struct SpeciesDef {
    int         id = 0;          // 1-based, stable — a save stores this, not a name
    std::string name;
    int         type = 0;
    int         hp = 39, atk = 52, def = 43, spd = 65;
    int         catch_rate = 190;   // 3..255, the classic scale
    int         evolve_to = 0;      // species id, 0 = final form
    int         evolve_at = 0;      // level
    std::string sprite;             // asset path of the .hrt this species wears
    // level -> move name, in the order they are learned. A creature knows the LAST
    // four it is eligible for, which is what makes levelling feel like a decision
    // being made for you rather than a list growing.
    std::vector<std::pair<int, std::string>> learnset;
};

// Where a wild creature comes from. One line per patch of long grass:
//
//     table route1 1:20:3-5 4:20:3-5 7:15:4-6
//
//  = species 1 at weight 20, levels 3..5. Weights are relative and need not sum to
//  anything, because a table someone is balancing gets entries added to it and
//  renormalising by hand is how a rare creature silently becomes common.
struct EncounterEntry {
    int species = 0;
    int weight  = 10;
    int lo = 2, hi = 4;
};

struct EncounterTable {
    std::string                 name;
    std::vector<EncounterEntry> entries;
    [[nodiscard]] int total_weight() const;
};

struct Dex {
    TypeChart                   types;
    std::vector<MoveDef>        moves;
    std::vector<SpeciesDef>     species;
    std::vector<EncounterTable> tables;

    [[nodiscard]] int move_index(const std::string& name) const;
    [[nodiscard]] const MoveDef* move(int index) const;
    [[nodiscard]] const SpeciesDef* species_by_id(int id) const;
    [[nodiscard]] const EncounterTable* table(const std::string& name) const;
};

// Parse one definitions file into `into`. All three record kinds may appear in any
// file, so a small game keeps one and this one keeps three. Returns false and sets
// `why` (when non-null) to a message naming the line.
//
// Order matters and is checked: a `move` may not name a type declared later in the
// same run, because the alternative is a two-pass parser whose error messages can no
// longer say which line was wrong.
bool parse_into(Dex& into, const std::string& text, std::string* why = nullptr);

// The four files a Dex is made of, in the order they must be parsed (a `move` may
// not name a type declared later). ONE list: three callers used to keep their own
// copy of it, and a file added to one of them was a file the other two did not read.
inline constexpr const char* kDexFiles[] = {
    "creatures/types.def",
    "creatures/moves.def",
    "creatures/species.def",
    "creatures/encounters.def",
};

// Parse all four through a caller-supplied reader. Pure: the reader does the I/O, so
// this same function serves the game (assets::), a test (assets::) and a headless
// command, and none of them can be reading a different set of files from the others.
// The reader returns false when the file is not there.
bool load_dex(Dex& d, const std::function<bool(const char*, std::string&)>& read,
              std::string* why = nullptr);

// A fingerprint of the rules a BATTLE is resolved under: the type chart, the moves
// and the species. A replay carries it, because the same actions produce a different
// battle after somebody re-tunes a move, and a verifier has to be able to say which
// of the two things went wrong (see replay.hpp).
//
// The encounter tables are deliberately NOT in it: `step` never reads them, so moving
// a creature to a different patch of grass must not invalidate every recording of a
// fight against one.
std::uint64_t rules_hash(const Dex& d);

// Everything the tables must satisfy before a battle can be trusted. Separate from
// parsing because a file can be well-formed and still describe an unplayable game:
// a species whose learnset is empty has no legal action on turn one, and an
// `evolve=` pointing at a species nobody declared is a dead end that only shows up
// at level 16 in someone else's playthrough.
std::vector<std::string> validate(const Dex& d);

} // namespace creature
