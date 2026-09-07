// =============================================================================
//  tests/test_elo.cpp  —  a rating, as integer arithmetic
// =============================================================================
//  The properties that matter are not "it computes Elo". They are: two machines
//  compute the SAME Elo, a match neither creates nor destroys rating, and the
//  curve is close enough to the real one that calling it Elo is honest.
// =============================================================================
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "engine/elo.hpp"
#include "engine/rand.hpp"

using namespace engine;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

namespace {

// The reference, used ONLY to say the table is honest. It is not the definition:
// `std::pow` is exactly what this file exists to keep out of the shipped path.
double reference(int diff) { return 1.0 / (1.0 + std::pow(10.0, -diff / 400.0)); }

void test_the_curve_is_elo() {
    double worst = 0.0;
    int    worst_at = 0;
    for (int d = -1200; d <= 1200; ++d) {
        const int    got  = elo_expected_permille(d);
        const int    used = d < -800 ? -800 : (d > 800 ? 800 : d);
        const double want = reference(used) * 1000.0;
        const double err  = std::fabs(got - want);
        if (err > worst) { worst = err; worst_at = d; }
    }
    std::printf("  worst curve error %.2f per mille (at diff %d)\n", worst, worst_at);
    CHECK(worst <= 2.0);

    // The three points anybody checking this by hand would check.
    CHECK(elo_expected_permille(0)    == 500);
    CHECK(elo_expected_permille(400)  == 909);
    CHECK(elo_expected_permille(-400) == 91);
}

void test_the_symmetry_is_exact() {
    // Not "within a per mille" — exact, because it is a mirror rather than a second
    // table. Everything below depends on it.
    for (int d = -2000; d <= 2000; ++d)
        CHECK(elo_expected_permille(d) + elo_expected_permille(-d) == 1000);
}

void test_the_curve_only_goes_one_way() {
    int last = -1;
    for (int d = -900; d <= 900; ++d) {
        const int e = elo_expected_permille(d);
        CHECK(e >= last);
        last = e;
        CHECK(e >= 0 && e <= 1000);
    }
    // ...and it is FLAT past the clamp, which is the deliberate part.
    CHECK(elo_expected_permille(800) == elo_expected_permille(5000));
    CHECK(elo_expected_permille(-800) == elo_expected_permille(-5000));
}

void test_a_match_is_zero_sum() {
    // What one player gains the other loses, exactly, at every rating pair — which
    // is only true because the rounding goes away from zero. Integer division
    // towards zero destroys a point on about half of all games, and a ladder that
    // leaks rating drifts down forever.
    long long created = 0;
    int checked = 0;
    for (int a = 100; a <= 2600; a += 37) {
        for (int b = 100; b <= 2600; b += 53) {
            const int a_win = elo_update(a, b, kEloWin);
            const int b_los = elo_update(b, a, kEloLoss);
            created += (a_win - a) + (b_los - b);
            CHECK((a_win - a) == -(b_los - b));

            const int a_dr = elo_update(a, b, kEloDraw);
            const int b_dr = elo_update(b, a, kEloDraw);
            created += (a_dr - a) + (b_dr - b);
            CHECK((a_dr - a) == -(b_dr - b));
            ++checked;
        }
    }
    CHECK(created == 0);
    CHECK(checked > 1000);
    std::printf("  %d rating pairs, zero rating created or destroyed\n", checked);
}

void test_the_directions() {
    // Beating someone better is worth more than beating someone worse, and both are
    // worth something. A K that rounded a small upset to zero would make the bottom
    // of a ladder unclimbable.
    const int equal = elo_update(1200, 1200, kEloWin) - 1200;
    const int upset = elo_update(1200, 1600, kEloWin) - 1200;
    const int easy  = elo_update(1200, 800,  kEloWin) - 1200;
    CHECK(equal == 16);                 // K/2, the one everybody knows
    CHECK(upset > equal);
    CHECK(easy  > 0);
    CHECK(easy  < equal);

    // ...and losing costs, in the mirror order.
    CHECK(elo_update(1200, 1200, kEloLoss) - 1200 == -16);
    CHECK(elo_update(1200, 800,  kEloLoss) < elo_update(1200, 1600, kEloLoss));

    // A draw against someone stronger is a gain, against someone weaker a loss.
    CHECK(elo_update(1200, 1600, kEloDraw) > 1200);
    CHECK(elo_update(1200, 800,  kEloDraw) < 1200);
    CHECK(elo_update(1200, 1200, kEloDraw) == 1200);
}

// The rounding rule, which turned out to be defended by the wrong argument. A
// mutation flipping it to plain integer division survived every test above — because
// truncation is symmetric, so the match stays zero-sum either way. What truncation
// actually costs is MAGNITUDE: it rounds a gain down and a loss up, every time.
void test_the_rounding_goes_away_from_zero() {
    // diff 1 puts the expectation at 501 per mille, so the winner's numerator is
    // 32 * 499 = 15968 — the .968 that truncation throws away.
    CHECK(elo_expected_permille(1) == 501);
    CHECK(elo_update(1201, 1200, kEloWin) - 1201 == 16);      // truncating gives 15
    CHECK(elo_update(1200, 1201, kEloLoss) - 1200 == -16);    // ...and -15
    // ...and it is still zero-sum, which is what the comment used to claim the
    // rounding was FOR. Both halves are asserted so neither can be quietly dropped.
    CHECK((elo_update(1201, 1200, kEloWin) - 1201) ==
          -(elo_update(1200, 1201, kEloLoss) - 1200));
}

void test_k_scales_it() {
    CHECK(elo_update(1200, 1600, kEloWin, 64) - 1200 ==
          2 * (elo_update(1200, 1600, kEloWin, 32) - 1200));
    CHECK(elo_update(1200, 1200, kEloWin, 0) == 1200);
}

void test_a_ladder_finds_the_better_player() {
    // The property a rating is FOR. Two players whose true strength differs play a
    // hundred games decided by that strength, and the ratings have to end up in the
    // right order by a clear margin — otherwise the number on the board is decoration.
    int strong = kEloStart, weak = kEloStart;
    // 70/30, from the project's own RNG at a fixed seed — deterministic, so this
    // never flakes, but SHUFFLED. The first version of this test used the block
    // pattern `(g % 10) < 7`, which is seven wins then three losses, and the gap
    // oscillated between 78 and ~300 with the sample always landing on the trough.
    // A rating responds to the ORDER of results, and a pattern with structure in it
    // measures the pattern.
    engine::Rng rng(0xE10);
    long long tail_sum = 0;
    int       tail_n   = 0;
    int       wins     = 0;
    for (int g = 0; g < 400; ++g) {
        const bool strong_won = rng.range(1, 100) <= 70;
        wins += strong_won ? 1 : 0;
        const int  s = strong, w = weak;
        strong = elo_update(s, w, strong_won ? kEloWin : kEloLoss);
        weak   = elo_update(w, s, strong_won ? kEloLoss : kEloWin);
        // The MEAN of the tail, not the last sample. With K = 32 the gap is a random
        // walk around its equilibrium, and one instant of it can be 100 points off in
        // either direction — asserting the last value would be asserting the noise.
        if (g >= 200) { tail_sum += strong - weak; ++tail_n; }
    }
    const int mean_gap = static_cast<int>(tail_sum / tail_n);
    CHECK(strong > weak);
    // The equilibrium for a 70% win rate is 400*log10(0.7/0.3) = 147 points, so the
    // gap is asserted to be APPROACHING that rather than merely positive — a rating
    // that separated by three points would also pass "strong > weak" and would be
    // useless. It is also asserted not to run away: a K that overshot would make the
    // board a record of who played most recently.
    CHECK(mean_gap > 100);
    CHECK(mean_gap < 220);
    CHECK(strong + weak == 2 * kEloStart);      // still zero-sum after four hundred
    std::printf("  %d/400 wins; mean gap over the last 200 games %d (equilibrium ~147)\n",
                wins, mean_gap);
}

} // namespace

int main() {
    test_the_curve_is_elo();
    test_the_symmetry_is_exact();
    test_the_curve_only_goes_one_way();
    test_a_match_is_zero_sum();
    test_the_directions();
    test_the_rounding_goes_away_from_zero();
    test_k_scales_it();
    test_a_ladder_finds_the_better_player();

    if (g_failures == 0) std::printf("test_elo: all checks passed\n");
    else                 std::printf("test_elo: %d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
