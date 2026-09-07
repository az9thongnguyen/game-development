// =============================================================================
//  engine/elo.hpp  —  a rating, as INTEGER arithmetic
// =============================================================================
//  The classic Elo formula is
//
//      E = 1 / (1 + 10^((opponent - me) / 400))
//
//  and every part of that line is a reason not to write it here. `std::pow` is not
//  required to be correctly rounded, so two C libraries may differ in the last bit;
//  the rating is then computed on the server and shown on the client, and the two
//  disagree by one point often enough for somebody to file it. This is the same
//  argument `battle.hpp` makes about the damage roll, one layer up: a number two
//  machines must agree about does not get to be a float.
//
//  So the curve is a TABLE — expected score in per mille for a rating difference,
//  every 8 points from 0 to 800, linearly interpolated in integers, and mirrored for
//  a negative difference so `E(d) + E(-d) == 1000` exactly. The table IS the
//  definition; `test_elo` checks it against the real formula to within 2 per mille,
//  which documents the approximation rather than depending on it.
//
//  Beyond 800 points the curve is flat to within a per mille, so it clamps. A player
//  1200 points ahead and a player 800 points ahead are both "should win", and
//  pretending otherwise would just make an upset pay differently for no reason.
//
//  PURE and header-only, like `rand.hpp`: no allocation, no I/O, no state.
// =============================================================================
#pragma once

#include <algorithm>

namespace engine {

// Where a player starts, and how much one game can move them. K = 32 is the classic
// "still settling" value: eight straight wins against equals move a new player ~120
// points, which is fast enough that a first session means something.
inline constexpr int kEloStart = 1200;
inline constexpr int kEloK     = 32;

// Score of the game, in the same per-mille unit as the expectation, so nothing in
// this file is ever a fraction.
inline constexpr int kEloWin  = 1000;
inline constexpr int kEloDraw = 500;
inline constexpr int kEloLoss = 0;

namespace detail {
// E(d) * 1000 for d = 0, 8, 16, ... 800.
inline constexpr int kEloCurve[101] = {
     500,  512,  523,  534,  546,  557,  569,  580,  591,  602,
     613,  624,  635,  645,  656,  666,  676,  686,  696,  706,
     715,  725,  734,  743,  751,  760,  768,  776,  784,  792,
     799,  807,  814,  820,  827,  834,  840,  846,  852,  858,
     863,  869,  874,  879,  884,  888,  893,  897,  901,  905,
     909,  913,  916,  920,  923,  926,  929,  932,  935,  938,
     941,  943,  946,  948,  950,  952,  954,  956,  958,  960,
     962,  963,  965,  966,  968,  969,  971,  972,  973,  974,
     975,  977,  978,  979,  980,  980,  981,  982,  983,  984,
     984,  985,  986,  986,  987,  988,  988,  989,  989,  990,
     990,
};
inline constexpr int kEloStep = 8;
inline constexpr int kEloMaxDiff = 800;
} // namespace detail

// Expected score, per mille, for `diff = my rating - theirs`.
[[nodiscard]] inline int elo_expected_permille(int diff) {
    const bool neg = diff < 0;
    const int  d   = std::min(neg ? -diff : diff, detail::kEloMaxDiff);
    const int  i   = d / detail::kEloStep;
    const int  f   = d % detail::kEloStep;
    const int  lo  = detail::kEloCurve[i];
    // The last entry has no successor to interpolate towards, and `f` is 0 there
    // anyway (d == 800 divides exactly) — but the read would still happen.
    const int  hi  = (i + 1 < 101) ? detail::kEloCurve[i + 1] : lo;
    const int  e   = lo + (hi - lo) * f / detail::kEloStep;
    // Mirrored rather than tabulated twice, which is what makes the symmetry exact
    // instead of approximately true — and the symmetry is what makes a match
    // zero-sum.
    return neg ? 1000 - e : e;
}

// The rating after one game. `score` is kEloWin / kEloDraw / kEloLoss.
//
// Rounds half AWAY FROM ZERO. It is worth being precise about what that buys and
// what it does not: the match is zero-sum EITHER WAY, because the winner's numerator
// and the loser's are exact negations and C++ integer division truncates
// symmetrically. (An earlier version of this comment claimed the rounding was what
// made it zero-sum. A mutation flipping it to plain division survived every test in
// `test_elo`, which is how the claim was found to be false.)
//
// What it buys is MAGNITUDE. Truncating always rounds a gain down and a loss up, so
// every close result is worth systematically less than it should be — a 15 where the
// arithmetic says 16, on about half of all games.
[[nodiscard]] inline int elo_update(int rating, int opponent, int score, int k = kEloK) {
    const int expected = elo_expected_permille(rating - opponent);
    const int num      = k * (score - expected);
    const int delta    = (num >= 0 ? num + 500 : num - 500) / 1000;
    return rating + delta;
}

// Deliberately NOT clamped to a floor. A floor is the one thing that would break the
// zero-sum property above, and a rating this arithmetic can actually reach — it takes
// dozens of straight losses to weaker players — is a true statement about the player.

} // namespace engine
