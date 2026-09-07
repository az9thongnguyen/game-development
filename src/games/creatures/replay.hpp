// =============================================================================
//  games/creatures/replay.hpp  —  a battle, as a file
// =============================================================================
//  `battle.hpp` spends its whole header comment on one claim: the same start and
//  the same actions produce the same battle on every machine. This is where that
//  claim stops being a property of a test running in one process and becomes a
//  FILE — something one machine writes and a different machine, built by a
//  different compiler for a different instruction set, reads and checks.
//
//  Three things are in the format, and each one exists because leaving it out
//  makes a different lie possible:
//
//   1. THE START STATE, not a seed. A seed reproduces a battle only if you also
//      know the parties, and the parties in this game arrive damaged.
//   2. A HASH AFTER EVERY TURN. The end hash alone says "you disagree"; the turn
//      hashes say WHEN, which is the only version of that sentence anyone can
//      debug. A verifier stops at the first mismatch, where the two states still
//      differ in one place instead of everywhere.
//   3. THE HASH OF THE RULES. A replay is a fact only relative to the tables it
//      was played under. Re-balance a move and every stored replay diverges — and
//      without this field that divergence is indistinguishable from a machine
//      getting the arithmetic wrong, which is the failure this whole format is
//      for. Deliberately NOT hashed: the encounter tables. `step` never reads
//      them, so changing where a creature is found must not invalidate a
//      recording of a fight against one.
//
//  PURE: builds a string, reads a string. The caller owns the file.
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "games/creatures/battle.hpp"
#include "games/creatures/defs.hpp"

namespace creature {

inline constexpr int kReplayVersion = 1;

// A file claiming more turns than this is refused before any allocation. Real
// battles end inside 30 turns; `test_creature`'s worst of a thousand was 28.
inline constexpr int kMaxReplayTurns = 4096;

struct Turn {
    Action        a0{}, a1{};
    std::uint64_t after = 0;   // hash(battle) once this turn resolved
};

struct Replay {
    std::uint64_t     rules = 0;   // rules_hash(dex) at recording time
    Battle            start;
    std::vector<Turn> turns;
};

// Replay from the start, ignoring the recorded hashes. This is the plain "what
// happens" answer; `verify` is the one that checks it against what was recorded.
Battle play(const Dex& d, const Replay& r);

// Serialise. Returns "" and sets `why` when the replay could not be written
// FAITHFULLY — a start that is already mid-battle or already over has fields this
// format (v1) does not carry, and writing it anyway would produce a file that
// verifies against a battle nobody played.
std::string write_replay(const Replay& r, std::string* why = nullptr);

// Parse. Refuses a version it does not know rather than skipping records it cannot
// read — the same rule as map2 and the creature save, and for the same reason.
bool read_replay(const Dex& d, const std::string& text, Replay& out,
                 std::string* why = nullptr);

struct Verdict {
    enum class Fault : std::uint8_t {
        None = 0,
        Rules,      // recorded under different tables; not a determinism failure
        Desync      // the same rules produced a different state
    };

    bool          ok    = false;
    Fault         fault = Fault::None;
    int           turn  = 0;    // 1-based turn that disagreed (0 = none)
    std::uint64_t want = 0, got = 0;
    Battle        final;        // what re-playing actually produced
    std::string   why;          // one line, for a human
};

// Re-play and check every recorded hash. A recording that stops before the battle
// ends is NOT a fault: a match still being played is a legitimate thing to hold.
// What is a fault is a hash that disagrees, and the turn it first disagreed on is
// the whole reason the hashes are per-turn.
Verdict verify(const Dex& d, const Replay& r);

// ---- the reference battle -------------------------------------------------------
//
// One fixed battle — fixed species, fixed levels, fixed seed, a fixed script — whose
// serialised form is COMMITTED at `creatures/reference.crep`. `test_creature` rebuilds
// it and compares bytes, exactly the way a `.recipe` or a `.pix` is re-baked and
// compared, and CI runs that test on Linux/x86_64/gcc while the file in the repo was
// written on macOS/arm64/clang. That byte comparison is the only thing in this
// project that has ever tested the sentence at the top of `battle.hpp` across two
// instruction sets; a thousand battles inside one process test that the code is a
// pure function, which is a different and much weaker claim.
//
// The script visits Move, Switch AND Ball on purpose. A reference made only of moves
// would prove four bytes a turn reproduce and say nothing about the other kinds.
// Re-bake with:  ./build/demo --cmd creature.record creatures/reference.crep
Replay reference_battle(const Dex& d);

inline constexpr const char* kReferencePath = "creatures/reference.crep";

} // namespace creature
