// =============================================================================
//  tests/test_netbattle.cpp  —  one battle, two machines, four bytes a turn
// =============================================================================
//  No sockets here. Two `NetBattle` objects are handed each other's frames, which
//  is exactly what a transport would do and nothing else — so this file can play a
//  thousand matches, deliver frames out of order, and make one side compute the
//  battle WRONG on purpose, all without a server.
//
//  The claim being tested is not "a message arrives". It is: two peers that never
//  exchange a health bar end at the same state, and when they do not, they both
//  say so, at the same turn.
// =============================================================================
#include <cstdio>
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/rand.hpp"
#include "games/creatures/defs.hpp"
#include "games/creatures/netbattle.hpp"
#include "games/creatures/replay.hpp"

using namespace creature;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

namespace {

bool read_asset(const char* path, std::string& out) {
    const auto bytes = assets::load_file(path);
    if (!bytes) return false;
    out.assign(bytes->begin(), bytes->end());
    return true;
}

Dex shipped_dex() {
    Dex d;
    std::string why;
    if (!load_dex(d, read_asset, &why)) { std::printf("FAIL %s\n", why.c_str()); ++g_failures; }
    return d;
}

// A wire. `reverse` delivers each batch back-to-front, which is the cheapest way to
// say "there is no ordering between two sockets" — a protocol that only works when
// frames arrive in the order they were produced is a protocol that works on one
// machine.
struct Link {
    NetBattle a, b;
    bool      reverse = false;

    void pump(const Dex& da, const Dex& db) {
        std::vector<std::string> to_b = a.drain();
        std::vector<std::string> to_a = b.drain();
        if (reverse) {
            std::reverse(to_a.begin(), to_a.end());
            std::reverse(to_b.begin(), to_b.end());
        }
        for (const auto& f : to_a) a.on_frame(da, f);
        for (const auto& f : to_b) b.on_frame(db, f);
    }

    // Play until both sides stop owing an action. Returns the number of pumps.
    int play(const Dex& da, const Dex& db, int guard = 400) {
        int n = 0;
        for (; n < guard; ++n) {
            pump(da, db);
            const bool a_done = a.phase() == NetPhase::Over || a.phase() == NetPhase::Desync ||
                                a.phase() == NetPhase::Refused;
            const bool b_done = b.phase() == NetPhase::Over || b.phase() == NetPhase::Desync ||
                                b.phase() == NetPhase::Refused;
            if (a_done && b_done) break;
            if (a.phase() == NetPhase::MyTurn) a.act(da, choose(da, a.battle(), a.side()));
            if (b.phase() == NetPhase::MyTurn) b.act(db, choose(db, b.battle(), b.side()));
        }
        return n;
    }
};

Party party_of(const Dex& d, const std::vector<std::pair<int, int>>& spec) {
    return make_party(d, spec);
}

// -----------------------------------------------------------------------------
// TWO IDENTICAL TEAMS. This is the case that hung, and no test had it: every
// match above is built from randomly drawn species, so the two sides were never
// the same. `--pvp` handed both processes one fixed party, and the first real
// match between two processes ran five hundred turns and was still going — both
// sides out of PP, both rotating their benches at the other forever.
//
// Nothing was wrong with the protocol. The BATTLE had no way to end.
// -----------------------------------------------------------------------------
void test_a_mirror_match_ends(const Dex& d) {
    const std::vector<std::pair<int, int>> team = {{1, 20}, {5, 18}, {9, 22}};
    int drawn = 0, decided = 0, longest = 0;
    for (int n = 0; n < 12; ++n) {
        Link link;
        const std::uint64_t seed = 0x1BEEF + static_cast<std::uint64_t>(n) * 104729;
        link.a.begin(d, 0, seed, party_of(d, team));
        link.b.begin(d, 1, seed, party_of(d, team));
        // Deliberately more ticks than kMaxTurns: if the cap did not exist this loop
        // would run out rather than the battle ending, and the phase check below is
        // what tells those two apart.
        link.play(d, d, 3 * kMaxTurns);

        CHECK(link.a.phase() == NetPhase::Over);
        CHECK(link.b.phase() == NetPhase::Over);
        CHECK(hash(link.a.battle()) == hash(link.b.battle()));
        CHECK(static_cast<int>(link.a.tape().turns.size()) <= kMaxTurns);
        longest = std::max<int>(longest, static_cast<int>(link.a.tape().turns.size()));
        if (link.a.winner() < 0) ++drawn; else ++decided;

        // A capped battle is still a battle: its tape has to be a file like any
        // other, or a stalemate would be the one match nobody could review.
        std::string why;
        const std::string tape = write_replay(link.a.tape(), &why);
        CHECK(!tape.empty());
        Replay back;
        CHECK(read_replay(d, tape, back, &why));
        CHECK(verify(d, back).ok);
    }
    std::printf("  12 mirror matches: %d decided, %d drawn, longest %d turns (cap %d)\n",
                decided, drawn, longest, kMaxTurns);
    // With the AI fixed these DECIDE rather than merely stopping at the cap, which
    // is the better outcome and the one worth pinning: a mirror that always ran to
    // 200 and drew would mean the fix was the cap and nothing else. The cap itself
    // is proved where it can actually fire — `test_creature`'s no-PP stalemate.
    CHECK(decided == 12);
    CHECK(drawn == 0);
    CHECK(longest > 15);
    CHECK(longest < kMaxTurns);
}

// -----------------------------------------------------------------------------
// A thousand matches. Both peers end at the same state, holding the same tape, and
// the tape they built live is a file the offline verifier accepts.
// -----------------------------------------------------------------------------
void test_a_thousand_matches(const Dex& d) {
    engine::Rng meta(0x5EED5);
    int decided = 0, longest = 0, desyncs = 0, refused = 0;
    long long turns = 0;

    for (int n = 0; n < 1000; ++n) {
        Link  link;
        const int size = meta.range(1, 3);
        std::vector<std::pair<int, int>> pa, pb;
        for (int i = 0; i < size; ++i) {
            pa.emplace_back(meta.range(1, 18), meta.range(8, 25));
            pb.emplace_back(meta.range(1, 18), meta.range(8, 25));
        }
        const std::uint64_t seed = meta.next();
        link.reverse = (n % 2) == 0;               // half the matches out of order
        link.a.begin(d, 0, seed, party_of(d, pa));
        link.b.begin(d, 1, seed, party_of(d, pb));

        link.play(d, d);

        if (link.a.phase() == NetPhase::Desync || link.b.phase() == NetPhase::Desync) ++desyncs;
        if (link.a.phase() == NetPhase::Refused || link.b.phase() == NetPhase::Refused) ++refused;
        if (link.a.phase() != NetPhase::Over || link.b.phase() != NetPhase::Over) continue;

        // THE assertion: two machines, no state exchanged, same battle.
        if (hash(link.a.battle()) != hash(link.b.battle())) {
            if (++desyncs <= 3) std::printf("FAIL match %d ended in two different states\n", n);
            continue;
        }
        if (link.a.winner() != link.b.winner()) ++desyncs;
        if (link.a.won() == link.b.won() && link.a.winner() >= 0) ++desyncs;  // exactly one wins
        if (link.a.winner() >= 0) ++decided;

        // ...and the two recordings are the same FILE, byte for byte.
        std::string wa, wb;
        const std::string ta = write_replay(link.a.tape(), &wa);
        const std::string tb = write_replay(link.b.tape(), &wb);
        if (ta.empty() || ta != tb) {
            if (++desyncs <= 3) std::printf("FAIL match %d produced two different tapes\n", n);
            continue;
        }
        // ...which the OFFLINE verifier accepts. This is the join between chapter 138
        // and this one: a live match leaves behind exactly the artefact the file
        // format was built for.
        Replay back;
        std::string why;
        if (!read_replay(d, ta, back, &why) || !verify(d, back).ok) {
            if (++desyncs <= 3) std::printf("FAIL match %d tape does not verify: %s\n", n, why.c_str());
            continue;
        }

        longest = std::max<int>(longest, static_cast<int>(link.a.tape().turns.size()));
        turns  += static_cast<long long>(link.a.tape().turns.size());
    }

    CHECK(desyncs == 0);
    CHECK(refused == 0);
    // The sample has to have shape, or a thousand matches that ended on turn one
    // would agree perfectly and prove nothing.
    CHECK(decided > 950);
    CHECK(longest > 10);
    CHECK(turns > 4000);
    std::printf("  1000 matches, %lld turns, %d decided, longest %d\n", turns, decided, longest);
}

// -----------------------------------------------------------------------------
// The whole point: when the two sides DO compute different states, both find out,
// at the same turn, and neither declares a winner.
// -----------------------------------------------------------------------------
void test_a_desync_is_caught(const Dex& d) {
    // One peer is playing a build where a move hits harder. Everything else — the
    // seed, the sides, the actions — is identical, which is exactly the situation a
    // stale client is in.
    Dex tuned = d;
    CHECK(!tuned.moves.empty());
    tuned.moves[0].power += 7;

    int caught = 0, same_turn = 0, no_winner = 0, rounds = 0;
    for (int n = 0; n < 40; ++n) {
        Link link;
        const std::uint64_t seed = 0x0DE5 + static_cast<std::uint64_t>(n) * 7919;
        link.a.begin(d, 0, seed, party_of(d, {{1, 20}, {5, 18}}));
        link.b.begin(tuned, 1, seed, party_of(tuned, {{4, 19}, {8, 21}}));
        link.play(d, tuned);
        ++rounds;

        // Both sides, not one: a protocol where only the victim notices is a
        // protocol where the other player keeps playing a game that ended.
        if (link.a.phase() == NetPhase::Desync && link.b.phase() == NetPhase::Desync) {
            ++caught;
            if (link.a.desync_turn() == link.b.desync_turn()) ++same_turn;
            if (link.a.winner() < 0 && link.b.winner() < 0) ++no_winner;
            // ...and each side reports the OTHER's hash as the one it disagrees with.
            CHECK(link.a.mine_hash() == link.b.their_hash());
            CHECK(link.a.their_hash() == link.b.mine_hash());
            CHECK(link.a.mine_hash() != link.a.their_hash());
        }
    }
    std::printf("  %d/%d tuned-client matches desynced, %d at the same turn\n",
                caught, rounds, same_turn);
    CHECK(caught >= 35);         // move 0 is common enough that nearly all diverge
    CHECK(same_turn == caught);
    CHECK(no_winner == caught);

    // ...and the same pairing with the SAME tables must not desync, or the test above
    // is measuring the protocol being broken rather than the tables differing.
    int clean = 0;
    for (int n = 0; n < 40; ++n) {
        Link link;
        const std::uint64_t seed = 0x0DE5 + static_cast<std::uint64_t>(n) * 7919;
        link.a.begin(d, 0, seed, party_of(d, {{1, 20}, {5, 18}}));
        link.b.begin(d, 1, seed, party_of(d, {{4, 19}, {8, 21}}));
        link.play(d, d);
        if (link.a.phase() == NetPhase::Over && link.b.phase() == NetPhase::Over) ++clean;
    }
    CHECK(clean == 40);
}

// -----------------------------------------------------------------------------
// The side and the seed come from the server, and nothing about the ORDER two
// peers talk in may change the battle.
// -----------------------------------------------------------------------------
void test_the_server_decides(const Dex& d) {
    const Party pa = party_of(d, {{1, 15}, {2, 15}});
    const Party pb = party_of(d, {{4, 15}, {5, 15}});

    // Side 0's party is in slot 0 on BOTH machines, whichever received first.
    for (int order = 0; order < 2; ++order) {
        NetBattle a, b;
        a.begin(d, 0, 0xABCDEFull, pa);
        b.begin(d, 1, 0xABCDEFull, pb);
        auto fa = a.drain(), fb = b.drain();
        if (order == 0) { for (auto& f : fa) b.on_frame(d, f); for (auto& f : fb) a.on_frame(d, f); }
        else            { for (auto& f : fb) a.on_frame(d, f); for (auto& f : fa) b.on_frame(d, f); }
        CHECK(a.phase() == NetPhase::MyTurn);
        CHECK(b.phase() == NetPhase::MyTurn);
        CHECK(hash(a.battle()) == hash(b.battle()));
        CHECK(a.battle().side[0].member[0].species == 1);
        CHECK(b.battle().side[0].member[0].species == 1);
        CHECK(a.battle().rng == 0xABCDEFull);
    }

    // A different seed is a different battle. The server picking it is what stops a
    // player picking a favourable one; this asserts the seed reaches the sim at all.
    Link one, two;
    one.a.begin(d, 0, 1, pa);  one.b.begin(d, 1, 1, pb);
    two.a.begin(d, 0, 2, pa);  two.b.begin(d, 1, 2, pb);
    one.play(d, d);
    two.play(d, d);
    CHECK(hash(one.a.battle()) != hash(two.a.battle()));

    // Stats are DERIVED from what the wire carries, so a party survives the trip
    // exactly. `make_party` on both ends, never a stat block.
    Party round;
    CHECK(read_party(d, write_party(pa), round));
    CHECK(round.count == pa.count);
    for (int i = 0; i < round.count; ++i) {
        CHECK(round.member[i].species == pa.member[i].species);
        CHECK(round.member[i].max_hp  == pa.member[i].max_hp);
        CHECK(round.member[i].atk     == pa.member[i].atk);
        CHECK(round.member[i].moves[0].move == pa.member[i].moves[0].move);
    }
}

// -----------------------------------------------------------------------------
// What a peer may not say. Every one of these is a frame that, honoured, would put
// this side in a battle the other side is not in.
// -----------------------------------------------------------------------------
void test_the_protocol_refuses(const Dex& d) {
    const Party mine = party_of(d, {{1, 10}});

    struct Case { const char* what; std::string frame; };
    const std::vector<Case> bad = {
        {"an empty frame",             ""},
        {"an unknown frame",           "greetings 1"},
        {"a party with no version",    "party"},
        {"a protocol from the future", "party 2 1:10"},
        {"an empty party",             "party 1 "},
        {"a species nobody declared",  "party 1 999:10"},
        {"a level nobody can reach",   "party 1 1:900"},
        {"a party of seven",           "party 1 1:5 1:5 1:5 1:5 1:5 1:5 1:5"},
        {"a malformed pair",           "party 1 1-5"},
        {"a pair with junk in it",     "party 1 1:5x"},
    };
    for (const Case& c : bad) {
        NetBattle n;
        n.begin(d, 0, 7, mine);
        n.drain();
        if (n.on_frame(d, c.frame)) {
            std::printf("FAIL the protocol accepted %s\n", c.what);
            ++g_failures;
        }
        CHECK(n.phase() == NetPhase::Refused);
    }

    // ...and the good one, in the other direction: the same shape, accepted.
    {
        NetBattle n;
        n.begin(d, 0, 7, mine);
        n.drain();
        CHECK(n.on_frame(d, "party 1 4:10 8:12"));
        CHECK(n.phase() == NetPhase::MyTurn);
        // A SECOND party frame is refused — a peer that could re-declare its team
        // mid-match could re-declare it after seeing the first turn.
        CHECK(!n.on_frame(d, "party 1 4:10"));
        CHECK(n.phase() == NetPhase::Refused);
    }

    // Frames that arrive before the battle exists, and frames that reach too far
    // ahead of it.
    {
        NetBattle n;
        n.begin(d, 0, 7, mine);
        n.drain();
        CHECK(n.on_frame(d, "party 1 4:10"));
        CHECK(n.on_frame(d, "act 1 0 0"));            // one turn ahead: buffered
        CHECK(n.on_frame(d, "act 2 0 0"));            // two: the limit
        CHECK(!n.on_frame(d, "act 9 0 0"));           // nine: refused
        CHECK(n.phase() == NetPhase::Refused);
    }
    {
        NetBattle n;
        n.begin(d, 0, 7, mine);
        n.drain();
        CHECK(n.on_frame(d, "party 1 4:10"));
        CHECK(!n.on_frame(d, "act 1 9 0"));           // action kind 9
    }
    {
        NetBattle n;
        n.begin(d, 0, 7, mine);
        n.drain();
        CHECK(n.on_frame(d, "party 1 4:10"));
        CHECK(!n.on_frame(d, "hash 1 nothex"));
    }
    {
        NetBattle n;
        n.begin(d, 0, 7, mine);
        n.drain();
        CHECK(n.on_frame(d, "party 1 4:10"));
        CHECK(!n.on_frame(d, "hash 40 0000000000000000"));   // a turn that cannot exist
    }

    // A duplicate action for a turn already resolved is IGNORED rather than refused:
    // a resend is what a flaky connection does, and dropping the match for it would
    // make the ladder a test of somebody's wifi.
    {
        Link link;
        link.a.begin(d, 0, 99, party_of(d, {{1, 20}}));
        link.b.begin(d, 1, 99, party_of(d, {{4, 20}}));
        link.pump(d, d);
        CHECK(link.a.phase() == NetPhase::MyTurn);
        link.a.act(d, Action{Action::Kind::Move, 0});
        link.b.act(d, Action{Action::Kind::Move, 0});
        link.pump(d, d);
        CHECK(link.a.tape().turns.size() == 1);
        CHECK(link.a.on_frame(d, "act 1 0 0"));       // the same frame again
        CHECK(link.a.phase() != NetPhase::Refused);
        CHECK(link.a.tape().turns.size() == 1);       // ...and it changed nothing
    }
}

// -----------------------------------------------------------------------------
// A second match on the same object. `begin` resets, and until a mutation deleted
// the reset and survived, nothing said so — every test built a fresh NetBattle,
// while any client that plays twice reuses one.
// -----------------------------------------------------------------------------
void test_a_second_match_starts_clean(const Dex& d) {
    Link one;
    one.a.begin(d, 0, 111, party_of(d, {{1, 20}, {5, 20}}));
    one.b.begin(d, 1, 111, party_of(d, {{4, 20}, {8, 20}}));
    one.play(d, d);
    CHECK(one.a.phase() == NetPhase::Over);
    const std::size_t first_turns = one.a.tape().turns.size();
    CHECK(first_turns >= 2);

    // Same objects, different sides, different seed, different teams.
    one.a.begin(d, 1, 222, party_of(d, {{9, 15}}));
    one.b.begin(d, 0, 222, party_of(d, {{12, 15}}));
    CHECK(one.a.phase() != NetPhase::Over);
    CHECK(one.a.side() == 1);
    CHECK(one.a.tape().turns.empty());          // no turns carried over
    one.play(d, d);
    CHECK(one.a.phase() == NetPhase::Over);
    CHECK(one.b.phase() == NetPhase::Over);
    CHECK(hash(one.a.battle()) == hash(one.b.battle()));
    CHECK(one.a.battle().side[0].member[0].species == 12);   // b was side 0 this time
    // The second tape is the second match, not the first one with more on the end.
    std::string why;
    Replay back;
    CHECK(read_replay(d, write_replay(one.a.tape(), &why), back, &why));
    CHECK(verify(d, back).ok);
    CHECK(back.start.rng == 222);
}

// -----------------------------------------------------------------------------
// The action is FOUR BYTES, and that is the sentence the whole design is for.
// -----------------------------------------------------------------------------
void test_a_turn_is_small(const Dex& d) {
    Link link;
    link.a.begin(d, 0, 4242, party_of(d, {{1, 20}, {5, 20}, {9, 20}}));
    link.b.begin(d, 1, 4242, party_of(d, {{4, 20}, {8, 20}, {12, 20}}));
    link.pump(d, d);

    std::size_t largest_turn = 0, frames = 0;
    for (int i = 0; i < 200 && link.a.phase() != NetPhase::Over; ++i) {
        if (link.a.phase() == NetPhase::MyTurn) link.a.act(d, choose(d, link.a.battle(), 0));
        if (link.b.phase() == NetPhase::MyTurn) link.b.act(d, choose(d, link.b.battle(), 1));
        std::size_t bytes = 0;
        for (const auto& f : link.a.drain()) { bytes += f.size(); ++frames; link.b.on_frame(d, f); }
        for (const auto& f : link.b.drain()) { link.a.on_frame(d, f); }
        largest_turn = std::max(largest_turn, bytes);
    }
    CHECK(link.a.phase() == NetPhase::Over);
    // An action frame and a hash frame per turn. The hash is 16 hex characters and
    // dwarfs the action, which is the honest number: verifying the turn costs four
    // times what playing it does, and it is still nothing.
    CHECK(largest_turn < 48);
    std::printf("  a turn costs at most %zu bytes on the wire (%zu frames sent)\n",
                largest_turn, frames);
}

} // namespace

int main() {
    assets::set_base_path(ASSET_ROOT "/assets");
    const Dex d = shipped_dex();
    if (d.species.empty()) return 1;

    test_the_server_decides(d);
    test_the_protocol_refuses(d);
    test_a_turn_is_small(d);
    test_a_second_match_starts_clean(d);
    test_a_mirror_match_ends(d);
    test_a_desync_is_caught(d);
    test_a_thousand_matches(d);

    if (g_failures == 0) std::printf("test_netbattle: all checks passed\n");
    else                 std::printf("test_netbattle: %d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
