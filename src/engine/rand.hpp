// =============================================================================
//  engine/rand.hpp  —  the one deterministic RNG
// =============================================================================
//  xorshift64*. Small, fast, and — the only property that actually matters here —
//  IDENTICAL on every platform. `std::mt19937` is portable, but the distributions
//  are not: `std::uniform_int_distribution` is implementation-defined, so the same
//  seed gives a different sequence under libc++ and libstdc++. A save that replays
//  differently on the web build than on the desktop is not a save, and a PvP battle
//  that diverges between two players is not a battle.
//
//  This is the THIRD place that sentence needed to be true (the farm's day roll, and
//  now a battle), so it stops being a paragraph copied into a game and becomes one
//  header. The particle system keeps its own xorshift32 on purpose: it seeds per
//  emitter and never has to agree with anything, and merging it would make a visual
//  detail share a contract with a save file.
//
//  PURE: no I/O, header-only, no dependency past <cstdint>.
// =============================================================================
#pragma once

#include <cstdint>

namespace engine {

class Rng {
public:
    // Seed 0 would make xorshift64 emit zeros forever, so it becomes the golden
    // ratio constant instead of being rejected: a caller that hashes a name into a
    // seed should not have to special-case the one name that hashes to nothing.
    explicit Rng(std::uint64_t seed) : s_(seed ? seed : 0x9E3779B97F4A7C15ull) {}

    std::uint64_t next() {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return s_ * 0x2545F4914F6CDD1Dull;
    }

    // Inclusive on both ends. `lo > hi` returns `lo` and consumes NOTHING — an empty
    // range must not move the stream, or a battle where one side happens to have no
    // legal choice would desync against the same battle where it does.
    int range(int lo, int hi) {
        if (lo >= hi) return lo;
        const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1;
        return lo + static_cast<int>(next() % span);
    }

    // The state IS the position in the stream, so anything that must survive a save
    // or travel to another machine stores this rather than the seed plus a count.
    [[nodiscard]] std::uint64_t state() const { return s_; }
    void set_state(std::uint64_t s) { s_ = s ? s : 0x9E3779B97F4A7C15ull; }

private:
    std::uint64_t s_;
};

} // namespace engine
