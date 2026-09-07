// =============================================================================
//  tests/test_creature.cpp  —  a thousand battles, played twice
// =============================================================================
//  The claim this file exists to check is not "the battle works". It is:
//
//      the same start state and the same actions produce the same battle.
//
//  So the centre of the file is a loop that plays a thousand random battles to
//  their end, records the actions, replays them, and compares the hash AFTER EVERY
//  TURN. Comparing only the final hash would find the divergence at the point where
//  every field already differs; comparing per turn names the turn it happened on,
//  which is the difference between a failing test and a usable one.
//
//  The tables it plays with are the REAL ones in assets/creatures/. A fixture would
//  keep passing while the shipped balance file stopped parsing.
// =============================================================================
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif

#include "engine/assets.hpp"
#include "engine/rand.hpp"
#include "games/creatures/battle.hpp"
#include "games/creatures/replay.hpp"
#include "games/creatures/defs.hpp"
#include "engine/mix/mix.hpp"

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

bool read_asset(const char* path, std::string& out) {
    const auto bytes = assets::load_file(path);
    if (!bytes) return false;
    out.assign(bytes->begin(), bytes->end());
    return true;
}

Dex load_shipped_dex() {
    Dex d;
    std::string why;
    if (!load_dex(d, read_asset, &why)) {
        std::printf("FAIL %s\n", why.c_str());
        ++g_failures;
    }
    return d;
}

// ---- 1. the tables -------------------------------------------------------------

void test_tables(const Dex& d) {
    CHECK(d.types.names.size() == 6);
    CHECK(d.moves.size() == 17);
    CHECK(d.species.size() == 18);

    const std::vector<std::string> bad = validate(d);
    for (const std::string& b : bad) std::printf("FAIL validate: %s\n", b.c_str());
    CHECK(bad.empty());

    const int fire = d.types.index("fire"), grass = d.types.index("grass");
    const int water = d.types.index("water"), rock = d.types.index("rock");
    CHECK(d.types.multiplier(fire, grass) == 200);
    CHECK(d.types.multiplier(grass, fire) == 50);
    CHECK(d.types.multiplier(water, rock) == 200);
    // The unstated majority: anything with no `eff` line is neutral, and that is the
    // property that lets the file be exceptions-only.
    CHECK(d.types.multiplier(d.types.index("normal"), water) == kNeutral);
    // Growing the chart re-lays it row-major. If that were an append, the LAST type
    // declared would read its row out of the previous layout — so check a corner.
    CHECK(d.types.multiplier(rock, fire) == 200);
    CHECK(d.types.multiplier(rock, rock) == 50);

    // Six lines of three: every stage-1 evolves, every stage-3 does not, and the
    // chain is closed (an evolve= naming nothing is caught by validate above).
    int finals = 0;
    for (const SpeciesDef& s : d.species) if (s.evolve_to == 0) ++finals;
    CHECK(finals == 6);

    // `pri=1` has exactly one consumer, on purpose. If that ever becomes zero the
    // priority branch in `step` is dead code that nothing would notice.
    int priority_moves = 0;
    for (const MoveDef& m : d.moves) if (m.priority != 0) ++priority_moves;
    CHECK(priority_moves == 1);

    int status_moves = 0;
    for (const MoveDef& m : d.moves) if (m.power == 0) ++status_moves;
    CHECK(status_moves >= 1);
}

// ---- 2. the parser says no -----------------------------------------------------

void test_parser_refusals() {
    const std::string base = "type fire\ntype grass\nmove ember type=fire power=40\n";

    struct Case { const char* text; const char* what; };
    const Case bad[] = {
        {"type fire\ntype fire\n",                          "a type declared twice"},
        {"type fire\neff fire ice 200\n",                   "eff naming an unknown type"},
        {"type fire\neff fire fire 900\n",                  "an effectiveness out of range"},
        {"type fire\nmove ember type=ice\n",                "a move with an unknown type"},
        {"type fire\nmove ember type=fire\nmove ember type=fire\n", "a move declared twice"},
        {"type fire\nmove ember type=fire power=lots\n",    "power that is not a number"},
        {"type fire\nmove ember type=fire acc=0\n",         "accuracy of zero"},
        {"type fire\nmove ember type=fire effect=melt:10\n","an unknown effect"},
        {"type fire\nmove ember type=fire effect=burn:0\n", "an effect chance of zero"},
        {"type fire\nspecies x pup type=fire\n",            "a species id that is not a number"},
        {"type fire\nspecies 1 a type=fire\nspecies 1 b type=fire\n", "a species id twice"},
        {"type fire\nspecies 1 pup type=fire hp=0\n",       "a stat of zero"},
        {"type fire\nspecies 1 pup type=fire catch=300\n",  "a catch rate over 255"},
        {"type fire\nspecies 1 pup type=fire evolve=2\n",   "evolve with no level"},
        {"type fire\nspecies 1 pup type=fire moves=tackle\n", "a learnset entry with no level"},
        {"blorp\n",                                          "an unknown record"},
    };
    for (const Case& c : bad) {
        Dex d;
        std::string why;
        const bool ok = parse_into(d, c.text, &why);
        if (ok) std::printf("FAIL parser ACCEPTED %s\n", c.what);
        CHECK(!ok);
        CHECK(!why.empty());
    }

    // And the additive half: an unknown KEY must be ignored, or every later field is
    // a breaking change for every older file.
    Dex d;
    CHECK(parse_into(d, base + "move flare type=fire power=50 shininess=9\n", nullptr));
    CHECK(d.move_index("flare") >= 0);
}

// ---- 3. validate refuses a well-formed but unplayable game ---------------------

void test_validate() {
    Dex d;
    CHECK(parse_into(d, "type fire\nmove ember type=fire power=40\n"
                        "species 1 pup type=fire sprite=a.hrt moves=4:ember\n", nullptr));
    const auto bad = validate(d);
    // Nothing at level 1: a creature made at level 3 would have no legal action and
    // the battle would sit there forever rather than crash.
    bool found = false;
    for (const std::string& s : bad) if (s.find("level 1") != std::string::npos) found = true;
    CHECK(found);

    Dex e;
    CHECK(parse_into(e, "type fire\nmove ember type=fire power=40\n"
                        "species 1 pup type=fire sprite=a.hrt evolve=9@16 moves=1:ember\n",
                     nullptr));
    bool dangling = false;
    for (const std::string& s : validate(e)) if (s.find("unknown id 9") != std::string::npos) dangling = true;
    CHECK(dangling);
}

// ---- 4. a creature is built from its species and level -------------------------

void test_make(const Dex& d) {
    const Creature pup = make(d, 1, 5);
    CHECK(pup.species == 1);
    CHECK(pup.hp == pup.max_hp);
    CHECK(pup.max_hp == (39 * 2 * 5) / 100 + 5 + 10);
    // emberpup at 5 knows tackle(1) and ember(4) and nothing else yet.
    CHECK(pup.moves[0].move == d.move_index("tackle"));
    CHECK(pup.moves[1].move == d.move_index("ember"));
    CHECK(pup.moves[2].move == -1);

    // Eligible for exactly four: it keeps all four, tackle included.
    const Creature grown = make(d, 1, 40);
    CHECK(grown.moves[0].move == d.move_index("tackle"));
    CHECK(grown.moves[3].move == d.move_index("flamewheel"));

    // blazehound at 40 is eligible for FIVE, so the window slides and tackle falls
    // off the front. That is the whole reason a learnset is ordered rather than a
    // set, and it is the branch a `filled < 4` off-by-one would silently invert.
    const Creature five = make(d, 2, 40);
    CHECK(five.moves[0].move == d.move_index("ember"));
    CHECK(five.moves[3].move == d.move_index("inferno"));
    for (int i = 0; i < kMoveSlots; ++i) CHECK(five.moves[i].move != d.move_index("tackle"));

    // A species nobody declared is an EMPTY SLOT, not a crash and not a default
    // creature — a party with a typo in it must be visibly short, not quietly wrong.
    CHECK(make(d, 999, 5).species == 0);

    const Party p = make_party(d, {{1, 5}, {999, 5}, {4, 5}});
    CHECK(p.count == 2);
    CHECK(p.member[1].species == 4);
}

// ---- 5. the turn ---------------------------------------------------------------

Battle two_up(const Dex& d, int a, int b, int level, std::uint64_t seed) {
    Battle bt;
    bt.side[0] = make_party(d, {{a, level}});
    bt.side[1] = make_party(d, {{b, level}});
    bt.rng = seed;
    return bt;
}

void test_turn(const Dex& d) {
    // Speed decides order. thunderpaw (spd 133) against pebbling (spd 22) at the
    // same level: the mouse always moves first, so the rock takes damage on turn 1
    // even in the battles where it faints.
    {
        Battle b = two_up(d, 12, 13, 30, 1234);
        std::vector<Event> ev;
        step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0}, &ev);
        CHECK(!ev.empty());
        CHECK(ev[0].kind == Event::Kind::Used);
        CHECK(ev[0].side == 0);
    }

    // Priority beats speed. tsunamaw's aquajet (pri 1) resolves before thunderpaw's
    // move despite giving away 40 points of speed.
    {
        Battle b = two_up(d, 6, 12, 30, 99);
        // tsunamaw at 30 knows aquajet in slot 0 (learnset order 1:aquajet, 1:bubble).
        CHECK(d.move(b.side[0].now().moves[0].move)->priority == 1);
        std::vector<Event> ev;
        step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0}, &ev);
        CHECK(ev[0].side == 0);
    }

    // A switch outruns everything, and the creature that came in is the one that
    // takes the hit.
    {
        Battle b;
        b.side[0] = make_party(d, {{13, 20}, {12, 20}});   // slow rock, fast mouse
        b.side[1] = make_party(d, {{3, 20}});
        b.rng = 77;
        std::vector<Event> ev;
        step(d, b, Action{Action::Kind::Switch, 1}, Action{Action::Kind::Move, 0}, &ev);
        CHECK(b.side[0].active == 1);
        CHECK(ev[0].kind == Event::Kind::Switched);
        CHECK(b.side[0].member[0].hp == b.side[0].member[0].max_hp);   // the rock is untouched
    }

    // Effectiveness reaches the damage number, not just the log. Same attacker, same
    // level, same roll stream — only the defender's type differs.
    {
        Battle hot = two_up(d, 3, 9, 30, 5);      // fire vs grass: 200%
        Battle wet = two_up(d, 3, 6, 30, 5);      // fire vs water:  50%
        std::vector<Event> a, c;
        step(d, hot, Action{Action::Kind::Move, 1}, Action{Action::Kind::Move, 0}, &a);
        step(d, wet, Action{Action::Kind::Move, 1}, Action{Action::Kind::Move, 0}, &c);
        int super = 0, resisted = 0;
        for (const Event& e : a) if (e.kind == Event::Kind::Damage && e.side == 1) super = e.a;
        for (const Event& e : c) if (e.kind == Event::Kind::Damage && e.side == 1) resisted = e.a;
        CHECK(super > resisted * 2);
    }

    // A finished battle is finished: stepping again must not resurrect it.
    {
        Battle b = two_up(d, 3, 7, 40, 3);
        for (int i = 0; i < 200 && !b.over; ++i)
            step(d, b, choose(d, b, 0), choose(d, b, 1));
        CHECK(b.over);
        const std::uint64_t h = hash(b);
        step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0});
        CHECK(hash(b) == h);
    }

    // Running ends it and hands the win to the other side.
    {
        Battle b = two_up(d, 1, 4, 5, 11);
        std::vector<Event> ev;
        step(d, b, Action{Action::Kind::Run, 0}, Action{Action::Kind::Move, 0}, &ev);
        CHECK(b.over && b.fled && b.winner == 1);
    }

    // PP runs out and the move stops working — it does not wrap, and it does not
    // silently keep firing.
    {
        Battle b = two_up(d, 16, 16, 5, 21);
        b.side[0].now().moves[0].pp = 1;
        std::vector<Event> ev;
        step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0}, &ev);
        ev.clear();
        step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0}, &ev);
        bool nopp = false;
        for (const Event& e : ev) if (e.kind == Event::Kind::NoPP && e.side == 0) nopp = true;
        CHECK(nopp);
        CHECK(b.side[0].now().moves[0].pp == 0);
    }

    // A fainted active is replaced from the bench, and the replacement does not act
    // on the turn it arrives.
    {
        Battle b;
        b.side[0] = make_party(d, {{1, 5}, {4, 5}});
        b.side[1] = make_party(d, {{15, 50}});
        b.rng = 8;
        b.side[0].now().hp = 1;
        std::vector<Event> ev;
        step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0}, &ev);
        CHECK(b.side[0].active == 1);
        CHECK(!b.over);
    }
}

void test_status(const Dex& d) {
    // Burn ticks at end of turn and halves the burned creature's damage. Force it
    // rather than fishing for a 10% proc: the roll is the sim's business, the
    // consequence is what this asserts.
    Battle b = two_up(d, 16, 16, 20, 4242);
    b.side[0].now().status = Status::Burn;
    const int before = b.side[0].now().hp;
    std::vector<Event> ev;
    step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0}, &ev);
    bool hurt = false;
    for (const Event& e : ev)
        if (e.kind == Event::Kind::StatusHurt && e.side == 0) hurt = true;
    CHECK(hurt);
    CHECK(b.side[0].now().hp < before);

    // Sleep costs the turn, and the counter runs down to a wake-up.
    Battle s = two_up(d, 16, 16, 20, 7);
    s.side[0].now().status = Status::Sleep;
    s.side[0].now().sleep   = 2;
    const int hp1 = s.side[1].now().hp;
    step(d, s, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0});
    CHECK(s.side[1].now().hp == hp1);              // the sleeper hit nothing
    step(d, s, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0});
    CHECK(s.side[0].now().status == Status::None);  // woke up on the second turn

    // Paralysis halves speed, which is enough to flip the turn order between two
    // otherwise identical creatures.
    Battle p = two_up(d, 17, 17, 20, 31);
    p.side[0].now().status = Status::Paralyze;
    std::vector<Event> pe;
    step(d, p, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0}, &pe);
    CHECK(!pe.empty());
    CHECK(pe[0].side == 1);
}

void test_catch(const Dex& d) {
    // A full-health stage-3 is nearly uncatchable; the same creature at 1 HP and
    // asleep is nearly certain. Run the band rather than one roll — the formula is
    // the claim, not any single outcome.
    int hard = 0, easy = 0;
    for (int i = 0; i < 200; ++i) {
        Battle a = two_up(d, 16, 3, 30, 1000 + i);
        if (try_catch(d, a, 1)) ++hard;
        Battle b = two_up(d, 16, 3, 30, 1000 + i);
        b.side[1].now().hp = 1;
        b.side[1].now().status = Status::Sleep;
        if (try_catch(d, b, 1)) ++easy;
    }
    CHECK(hard < 30);
    CHECK(easy > 40);
    CHECK(easy > hard * 3);

    // The other end of the same formula: a stage-1 with a 190 catch rate, at 1 HP
    // and asleep, saturates the 255 scale and is caught every time. A band that only
    // ever tested the hard case would pass with the status bonus deleted.
    int always = 0;
    for (int i = 0; i < 100; ++i) {
        Battle b = two_up(d, 16, 1, 10, 4000 + i);
        b.side[1].now().hp = 1;
        b.side[1].now().status = Status::Sleep;
        if (try_catch(d, b, 1)) ++always;
    }
    CHECK(always == 100);

    // A catch ENDS the battle and advances the same stream a move would have, so a
    // replay that contains one stays in step.
    Battle b = two_up(d, 16, 1, 5, 5);
    const std::uint64_t before = b.rng;
    try_catch(d, b, 1);
    CHECK(b.rng != before);
}

// ---- 6. the thousand ------------------------------------------------------------

void test_thousand_replays(const Dex& d) {
    engine::Rng meta(0xC0FFEE);
    int desyncs = 0, decided = 0, longest = 0;
    long long total_turns = 0;

    for (int n = 0; n < 1000; ++n) {
        Battle start;
        // Levels are drawn once per battle and shared, so the two sides are matched:
        // a sample of blowouts would replay perfectly after two turns and prove very
        // little. Party size 2..4 so switching, fainting and benches all occur.
        const int size  = meta.range(2, 4);
        const int level = meta.range(12, 40);
        std::vector<std::pair<int, int>> a, b;
        for (int i = 0; i < size; ++i) {
            a.emplace_back(meta.range(1, 18), level + meta.range(-2, 2));
            b.emplace_back(meta.range(1, 18), level + meta.range(-2, 2));
        }
        start.side[0] = make_party(d, a);
        start.side[1] = make_party(d, b);
        start.rng     = meta.next();

        Replay r;
        r.start = start;
        r.rules = rules_hash(d);

        // Play it once, recording what happened AND the hash after every turn.
        Battle live = start;
        std::vector<std::uint64_t> marks;
        for (int t = 0; t < 300 && !live.over; ++t) {
            // Mostly the AI, sometimes a random legal-ish action, so the replay has
            // to survive switches and wasted turns rather than one tidy script.
            Action a0 = choose(d, live, 0);
            Action a1 = choose(d, live, 1);
            if (meta.range(0, 9) == 0) a0 = Action{Action::Kind::Switch, meta.range(0, 5)};
            if (meta.range(0, 19) == 0) a1 = Action{Action::Kind::Move, meta.range(0, 3)};
            step(d, live, a0, a1);
            marks.push_back(hash(live));
            r.turns.push_back(Turn{a0, a1, hash(live)});
        }
        if (live.over && live.winner >= 0) ++decided;
        longest = std::max(longest, static_cast<int>(marks.size()));
        total_turns += static_cast<long long>(marks.size());

        // Replay it, comparing turn by turn.
        Battle again = start;
        for (std::size_t t = 0; t < r.turns.size(); ++t) {
            if (again.over) break;
            step(d, again, r.turns[t].a0, r.turns[t].a1);
            if (hash(again) != marks[t]) {
                if (++desyncs <= 3)
                    std::printf("FAIL battle %d desynced on turn %zu\n", n, t + 1);
                break;
            }
        }
        if (hash(again) != hash(live)) ++desyncs;
        // `play()` is the shipped entry point; it must agree with the loop above.
        if (hash(play(d, r)) != hash(live)) ++desyncs;
        // ...and so must the ROUND TRIP through text. This is the claim the file
        // format exists to make: a battle written out, parsed back by something that
        // never saw the original objects, and re-played, is the same battle.
        std::string why;
        const std::string text = write_replay(r, &why);
        Replay back;
        if (text.empty() || !read_replay(d, text, back, &why)) {
            if (++desyncs <= 3) std::printf("FAIL battle %d: %s\n", n, why.c_str());
        } else {
            const Verdict v = verify(d, back);
            if (!v.ok) {
                if (++desyncs <= 3) std::printf("FAIL battle %d: %s\n", n, v.why.c_str());
            } else if (hash(v.final) != hash(live)) {
                ++desyncs;
            }
        }
    }

    CHECK(desyncs == 0);
    // A thousand battles that all ended on turn one would replay perfectly and prove
    // nothing, so the shape of the sample is asserted too.
    CHECK(decided > 900);
    CHECK(longest > 20);
    CHECK(total_turns > 8000);   // ~11 turns a battle, not ~1
    std::printf("  1000 battles, %lld turns, %d decided, longest %d\n",
                total_turns, decided, longest);
}

void test_hash_is_sensitive(const Dex& d) {
    // A hash that ignores a field is a desync detector that cannot detect the
    // desync. Poke each field in turn and demand the hash moves.
    Battle base = two_up(d, 1, 4, 10, 42);
    const std::uint64_t h = hash(base);

    Battle x = base; x.side[0].now().hp -= 1;              CHECK(hash(x) != h);
    x = base; x.side[0].now().status = Status::Burn;       CHECK(hash(x) != h);
    x = base; x.side[0].now().sleep = 2;                   CHECK(hash(x) != h);
    x = base; x.side[0].now().moves[0].pp -= 1;            CHECK(hash(x) != h);
    x = base; x.side[0].active = 1;                        CHECK(hash(x) != h);
    x = base; x.rng ^= 1;                                  CHECK(hash(x) != h);
    x = base; x.turn += 1;                                 CHECK(hash(x) != h);
    x = base; x.over = true;                               CHECK(hash(x) != h);
    x = base; x.winner = 1;                                CHECK(hash(x) != h);
    x = base; x.fled = true;                               CHECK(hash(x) != h);
    x = base; x.side[1].now().hp -= 1;                     CHECK(hash(x) != h);
    // The BENCH counts too — a hash that only covered the active creature would miss
    // every switch-in and half of what a replay has to protect.
    x = base; x.side[0].member[3].species = 7;             CHECK(hash(x) != h);
    // And an identical battle built twice hashes the same, or none of the above
    // means anything.
    CHECK(hash(two_up(d, 1, 4, 10, 42)) == h);
}

void test_ai(const Dex& d) {
    // The AI reads the type chart: a fire creature facing grass picks its fire move
    // over its stronger-on-paper normal one.
    Battle b = two_up(d, 2, 9, 30, 6);
    Creature& me = b.side[0].now();
    me.moves[0] = MoveSlot{d.move_index("bodyslam"), 15};   // 75 power, neutral
    me.moves[1] = MoveSlot{d.move_index("ember"), 25};      // 40 power, STAB, 2x
    me.moves[2] = MoveSlot{-1, 0};
    me.moves[3] = MoveSlot{-1, 0};
    const Action a = choose(d, b, 0);
    CHECK(a.kind == Action::Kind::Move);
    CHECK(a.index == 1);

    // It does NOT draw from the battle stream. If it did, a replay recorded against
    // the AI would not reproduce under a human making the same choices.
    const std::uint64_t before = b.rng;
    choose(d, b, 0);
    choose(d, b, 1);
    CHECK(b.rng == before);

    // At low HP with a better matchup on the bench it switches; with a WORSE one it
    // stands and fights. Both directions, because a guard that never lifts is the
    // failure mode this project keeps finding.
    {
        Battle s;
        s.side[0] = make_party(d, {{1, 20}, {4, 20}});   // fire out, water benched
        s.side[1] = make_party(d, {{13, 20}});           // rock: water beats it, fire does not
        s.rng = 3;
        s.side[0].now().hp = 1;
        CHECK(choose(d, s, 0).kind == Action::Kind::Switch);
    }
    {
        Battle s;
        s.side[0] = make_party(d, {{4, 20}, {1, 20}});   // water out, fire benched
        s.side[1] = make_party(d, {{13, 20}});
        s.rng = 3;
        s.side[0].now().hp = 1;
        CHECK(choose(d, s, 0).kind == Action::Kind::Move);
    }
    // With no PP anywhere it still returns something legal rather than move slot 0
    // of an empty slot.
    {
        Battle s;
        s.side[0] = make_party(d, {{1, 5}, {4, 5}});
        s.side[1] = make_party(d, {{7, 5}});
        s.rng = 3;
        for (int i = 0; i < kMoveSlots; ++i) s.side[0].now().moves[i].pp = 0;
        CHECK(choose(d, s, 0).kind == Action::Kind::Switch);
    }
}

void test_no_free_lunch(const Dex& d) {
    // Different seeds must actually produce different battles, or "deterministic"
    // would be satisfied by a sim that ignores its RNG entirely.
    int distinct = 0;
    std::uint64_t first = 0;
    for (int i = 0; i < 20; ++i) {
        Battle b = two_up(d, 2, 5, 25, 1000 + static_cast<std::uint64_t>(i) * 7919);
        for (int t = 0; t < 100 && !b.over; ++t)
            step(d, b, choose(d, b, 0), choose(d, b, 1));
        const std::uint64_t h = hash(b);
        if (i == 0) first = h;
        else if (h != first) ++distinct;
    }
    CHECK(distinct > 10);
}


// ---- 7. the eighteen wear something, and evolution is one more part ------------
//
//  The sim above never opens a file. These two do, because the claim the art half of
//  this chapter makes is checkable and would otherwise be a sentence in a comment.

std::optional<mix::Mix> load_mix(const std::string& path) {
    const auto bytes = assets::load_file(path);
    if (!bytes) return std::nullopt;
    std::string why;
    return mix::parse_mix(std::string(bytes->begin(), bytes->end()), &why);
}

void test_every_species_wears_a_sprite(const Dex& d) {
    std::set<std::string> seen;
    for (const SpeciesDef& s : d.species) {
        const auto bytes = assets::load_file(s.sprite);
        CHECK(bytes.has_value());
        if (bytes) {
            CHECK(bytes->size() > 12);
            CHECK(std::memcmp(bytes->data(), "HRT1", 4) == 0);
            const std::uint32_t w = (static_cast<std::uint32_t>((*bytes)[4]) << 24) |
                                    (static_cast<std::uint32_t>((*bytes)[5]) << 16) |
                                    (static_cast<std::uint32_t>((*bytes)[6]) << 8) | (*bytes)[7];
            CHECK(w == 16);
        } else {
            std::printf("      %s wears a missing sprite: %s\n", s.name.c_str(), s.sprite.c_str());
        }
        // Two species sharing one picture would look like a mixer working and is the
        // easiest way for a copy-paste in species.def to survive review.
        CHECK(seen.insert(s.sprite).second);
        // And the sprite must be MIXED, not a drawing that wandered in: the whole
        // point of eighteen is that no one of them was drawn.
        CHECK(load_mix(s.sprite.substr(0, s.sprite.size() - 4) + ".mix").has_value());
    }
    CHECK(seen.size() == 18);
}

void test_evolution_is_one_more_part(const Dex& d) {
    int lines = 0;
    for (const SpeciesDef& first : d.species) {
        // Start of a line: nobody evolves INTO it.
        bool is_first = true;
        for (const SpeciesDef& o : d.species) if (o.evolve_to == first.id) is_first = false;
        if (!is_first) continue;
        ++lines;

        std::vector<mix::Mix> stages;
        for (const SpeciesDef* s = &first; s;
             s = s->evolve_to ? d.species_by_id(s->evolve_to) : nullptr) {
            const auto m = load_mix(s->sprite.substr(0, s->sprite.size() - 4) + ".mix");
            CHECK(m.has_value());
            if (!m) return;
            stages.push_back(*m);
        }
        CHECK(stages.size() == 3);
        if (stages.size() != 3) continue;

        for (std::size_t i = 0; i + 1 < stages.size(); ++i) {
            const mix::Mix& a = stages[i];
            const mix::Mix& b = stages[i + 1];
            // ONE more part, and every part of the earlier stage survives into the
            // later one. That is the sentence parts_creature.pix is written around,
            // and without this it is only a sentence.
            CHECK(b.parts.size() == a.parts.size() + 1);
            for (const mix::Mix::Part& pa : a.parts) {
                bool found = false;
                for (const mix::Mix::Part& pb : b.parts)
                    if (pb.sheet == pa.sheet && pb.index == pa.index &&
                        pb.x == pa.x && pb.y == pa.y) found = true;
                if (!found)
                    std::printf("      %s drops part %d when it evolves\n",
                                a.name.c_str(), pa.index);
                CHECK(found);
            }
            // The colour never moves along a line, or the family resemblance the
            // whole sheet exists for would be a coincidence of three hand-typed pairs.
            CHECK(b.swaps.size() == a.swaps.size());
            for (std::size_t k = 0; k < a.swaps.size() && k < b.swaps.size(); ++k) {
                CHECK(a.swaps[k].from == b.swaps[k].from);
                CHECK(a.swaps[k].to   == b.swaps[k].to);
            }
        }
        CHECK(stages[0].parts.size() == 2);   // a body and a face
        CHECK(stages[2].parts.size() == 4);   // ...plus a crest, plus a tail
    }
    CHECK(lines == 6);
}


// ---- 8. the RNG underneath ------------------------------------------------------
//
//  Everything above is deterministic ONLY because engine::Rng is. Two claims in that
//  header had nothing checking them, and a mutation walked straight through both:
//  the stream itself was never pinned, and "an empty range consumes nothing" was a
//  sentence.

void test_rng() {
    // GOLDEN. These four numbers are the contract with every other machine and every
    // future build: a replay recorded today has to reproduce on a web build compiled
    // by a different toolchain, and nothing else in the repo would notice the
    // sequence quietly changing. If this fails, every stored replay is already void.
    engine::Rng r(1);
    const std::uint64_t want[] = {0x47E4CE4B896CDD1Dull, 0xABCFA6A8E079651Dull,
                                  0xB9D10D8FEB731F57ull, 0x4DB418A0BB1B019Dull};
    for (const std::uint64_t w : want) CHECK(r.next() == w);

    // Seed 0 is remapped rather than rejected, and it must not be a zero stream.
    engine::Rng z(0);
    CHECK(z.next() == 0x0D83B3E29A21487Aull);

    // range() is uniform enough to be worth having: a thousand draws from 1..100
    // average near 50 rather than piling on one end.
    engine::Rng q(0xDEADBEEF);
    long long sum = 0;
    for (int i = 0; i < 1000; ++i) {
        const int v = q.range(1, 100);
        CHECK(v >= 1 && v <= 100);
        sum += v;
    }
    CHECK(sum > 45000 && sum < 55000);

    // AN EMPTY RANGE CONSUMES NOTHING. Without this, a battle where one side happens
    // to have a single legal choice would advance the stream differently from the
    // same battle where it has two, and the desync would look like a physics bug.
    engine::Rng e(12345);
    const std::uint64_t before = e.state();
    CHECK(e.range(7, 7) == 7);
    CHECK(e.range(9, 2) == 9);
    CHECK(e.state() == before);

    // state()/set_state() is a round trip, which is what lets a battle be saved
    // mid-turn rather than only at its start.
    engine::Rng a(99), b(1);
    a.next(); a.next();
    b.set_state(a.state());
    CHECK(a.next() == b.next());
}

// ---- 9. the ten things a mutation walked through --------------------------------

// Damage from ONE move, in a battle where the named side moves first.
int first_damage(const Dex& d, Battle b, int slot, int side = 0) {
    std::vector<Event> ev;
    const Action mine{Action::Kind::Move, slot};
    const Action theirs{Action::Kind::Move, 0};
    step(d, b, side == 0 ? mine : theirs, side == 0 ? theirs : mine, &ev);
    for (const Event& e : ev)
        if (e.kind == Event::Kind::Damage && e.side == 1 - side) return e.a;
    return -1;
}

void test_damage_modifiers(const Dex& d) {
    // STAB. Same attacker, same defender, same seed, two 40-power moves — one of them
    // matches the attacker's type. Both are neutral against normal, so the only
    // difference left in the arithmetic is the 150/100.
    {
        Battle b;
        b.side[0] = make_party(d, {{2, 20}});      // blazehound, fire, fast
        b.side[1] = make_party(d, {{16, 20}});     // fluffkin, normal, slow
        b.rng = 4242;
        Creature& me = b.side[0].now();
        me.moves[0] = MoveSlot{d.move_index("tackle"), 35};   // 40 power, no STAB
        me.moves[1] = MoveSlot{d.move_index("ember"),  25};   // 40 power, STAB
        const int plain = first_damage(d, b, 0);
        const int stab  = first_damage(d, b, 1);
        CHECK(plain > 0);
        CHECK(stab > plain);
        // 1.5x, allowing for the +2 constant the formula adds before the multiplier.
        CHECK(stab * 100 >= plain * 130);
    }

    // BURN halves what the burned creature deals. Forced rather than fished for.
    {
        Battle b;
        b.side[0] = make_party(d, {{2, 20}});
        b.side[1] = make_party(d, {{16, 20}});
        b.rng = 909;
        const int healthy = first_damage(d, b, 0);
        b.side[0].now().status = Status::Burn;
        const int burned = first_damage(d, b, 0);
        CHECK(healthy > 0);
        CHECK(burned * 2 <= healthy + 2);
    }

    // A HIT THAT CONNECTS ALWAYS STINGS. A level-1 pup throwing a fire move at a
    // level-100 fire wolf that resists it: every factor in the formula pushes the
    // product below one, and the answer must still be 1. Zero damage reads as a miss
    // that was not a miss, and it is one deleted clamp away.
    //
    // The wolf is given a no-op action (a switch to the slot it is already in) rather
    // than a move, because otherwise it kills the pup before the pup can swing.
    {
        Battle b;
        b.side[0] = make_party(d, {{1, 1}});       // emberpup, level 1
        b.side[1] = make_party(d, {{3, 100}});     // pyrewolf, level 100, also fire
        b.rng = 5;
        b.side[0].now().moves[0] = MoveSlot{d.move_index("ember"), 25};
        std::vector<Event> ev;
        step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Switch, 0}, &ev);
        int dealt = -1;
        for (const Event& e : ev)
            if (e.kind == Event::Kind::Damage && e.side == 1) dealt = e.a;
        CHECK(dealt == 1);
    }

    // BURN ON A SMALL CREATURE. max_hp/16 is zero below sixteen HP, and a status that
    // does nothing for the first ten levels of the game is a bug nobody reports.
    {
        Battle b;
        b.side[0] = make_party(d, {{10, 1}});
        b.side[1] = make_party(d, {{10, 1}});
        b.rng = 6;
        CHECK(b.side[0].now().max_hp < 16);
        b.side[0].now().status = Status::Burn;
        const int before = b.side[0].now().hp;
        // BOTH sides get a no-op action. The first version of this let the opponent
        // attack, so the HP fell whether the burn ticked or not and the assertion was
        // satisfied by the wrong cause — the mutation walked straight through it.
        step(d, b, Action{Action::Kind::Switch, 0}, Action{Action::Kind::Switch, 0});
        CHECK(b.side[0].now().hp == before - 1);
    }
}

void test_the_coin_is_a_coin(const Dex& d) {
    // TWO IDENTICAL CREATURES. Priority ties, speed ties, and what is left is the
    // battle's own coin flip. "Side 0 first" is the cheap version of this rule and it
    // works right up until the two sides are two different computers.
    int went[2] = {0, 0};
    for (int i = 0; i < 60; ++i) {
        Battle b;
        b.side[0] = make_party(d, {{17, 20}});
        b.side[1] = make_party(d, {{17, 20}});
        b.rng = 1000 + static_cast<std::uint64_t>(i) * 7919;
        std::vector<Event> ev;
        step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0}, &ev);
        if (!ev.empty() && ev[0].kind == Event::Kind::Used) ++went[ev[0].side];
    }
    CHECK(went[0] > 10);
    CHECK(went[1] > 10);
}

void test_accuracy_and_paralysis(const Dex& d) {
    // A 90%-accurate move misses sometimes and a 100% one never does. Both halves:
    // "nothing ever misses" and "everything misses" are each one token away.
    int missed_90 = 0, missed_100 = 0, used = 0;
    for (int i = 0; i < 200; ++i) {
        Battle b;
        b.side[0] = make_party(d, {{14, 30}});     // bouldrin knows rockthrow, acc 90
        b.side[1] = make_party(d, {{16, 30}});
        b.rng = 500 + static_cast<std::uint64_t>(i) * 104729;
        Creature& me = b.side[0].now();
        me.moves[0] = MoveSlot{d.move_index("rockthrow"), 20};
        me.moves[1] = MoveSlot{d.move_index("tackle"),    35};
        std::vector<Event> ev;
        step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 3}, &ev);
        for (const Event& e : ev) {
            if (e.kind == Event::Kind::Used && e.side == 0) ++used;
            if (e.kind == Event::Kind::Missed && e.side == 0) ++missed_90;
        }
        Battle c = b;
        std::vector<Event> ce;
        step(d, c, Action{Action::Kind::Move, 1}, Action{Action::Kind::Move, 3}, &ce);
        for (const Event& e : ce)
            if (e.kind == Event::Kind::Missed && e.side == 0) ++missed_100;
    }
    CHECK(used == 200);
    CHECK(missed_90 > 5 && missed_90 < 60);   // ~10% of 200
    CHECK(missed_100 == 0);

    // Paralysis holds a creature about a quarter of the time — and, in the other
    // direction, not always.
    int held = 0, acted = 0;
    for (int i = 0; i < 200; ++i) {
        Battle b;
        b.side[0] = make_party(d, {{16, 20}});
        b.side[1] = make_party(d, {{16, 20}});
        b.rng = 31 + static_cast<std::uint64_t>(i) * 2654435761ull;
        b.side[0].now().status = Status::Paralyze;
        std::vector<Event> ev;
        step(d, b, Action{Action::Kind::Move, 0}, Action{Action::Kind::Move, 0}, &ev);
        for (const Event& e : ev) {
            if (e.kind == Event::Kind::Immobilised && e.side == 0) ++held;
            if (e.kind == Event::Kind::Used && e.side == 0) ++acted;
        }
    }
    CHECK(held > 20 && held < 90);
    CHECK(acted > 100);
}

void test_the_survivor_wins(const Dex& d) {
    // `winner` is an index, so it is exactly one token away from naming the side that
    // just died — and every "did it end" assertion in this file would still pass.
    for (int i = 0; i < 60; ++i) {
        Battle b;
        b.side[0] = make_party(d, {{static_cast<int>(1 + i % 18), 20}});
        b.side[1] = make_party(d, {{static_cast<int>(1 + (i * 5) % 18), 20}});
        b.rng = 77 + static_cast<std::uint64_t>(i) * 40503;
        for (int t = 0; t < 200 && !b.over; ++t)
            step(d, b, choose(d, b, 0), choose(d, b, 1));
        CHECK(b.over);
        if (b.winner < 0) continue;                 // a genuine double knockout
        CHECK(b.side[b.winner].any_alive());
        CHECK(!b.side[1 - b.winner].any_alive());
    }
}

void test_type_chart_survives_growth() {
    // The shipped types.def declares all six types BEFORE the first `eff` line, so
    // the re-layout inside `type` never has to preserve anything and a mutation that
    // deletes it passes every table check. Interleave them and it has work to do.
    Dex d;
    CHECK(parse_into(d, "type alpha\neff alpha alpha 50\n"
                        "type beta\neff beta alpha 200\n"
                        "type gamma\n", nullptr));
    const int a = d.types.index("alpha"), b = d.types.index("beta"),
              g = d.types.index("gamma");
    CHECK(d.types.multiplier(a, a) == 50);     // written when the chart was 1x1
    CHECK(d.types.multiplier(b, a) == 200);    // written when it was 2x2
    CHECK(d.types.multiplier(a, b) == kNeutral);
    CHECK(d.types.multiplier(g, g) == kNeutral);
    CHECK(d.types.eff.size() == 9);
}

} // namespace


// -----------------------------------------------------------------------------
// The reference battle, as committed bytes.
//
// Every other determinism check in this file runs the sim twice in ONE process,
// which proves it is a pure function of its inputs and nothing else. It cannot
// prove that two different compilers targeting two different instruction sets
// agree about what that function computes — and that is the claim battle.hpp
// actually makes. This one can, because the bytes in the repo were produced on
// macOS/arm64/clang and CI re-produces them on Linux/x86_64/gcc.
//
// A mismatch is not "regenerate the file". It is one of two things, and the test
// says which to go and find out.
// -----------------------------------------------------------------------------
void test_the_reference_battle_is_bytes(const Dex& d) {
    const Replay r = reference_battle(d);
    CHECK(r.turns.size() >= 10);

    // The script has to have actually reached the sim: a reference battle made of
    // nothing but moves would leave three of the four action kinds untested, and
    // the one that carries a NUMBER (a ball's bonus) is the one a format is most
    // likely to lose.
    int kinds[4] = {0, 0, 0, 0};
    for (const Turn& t : r.turns) { ++kinds[static_cast<int>(t.a0.kind)];
                                    ++kinds[static_cast<int>(t.a1.kind)]; }
    CHECK(kinds[static_cast<int>(Action::Kind::Move)]   > 0);
    CHECK(kinds[static_cast<int>(Action::Kind::Switch)] > 0);
    CHECK(kinds[static_cast<int>(Action::Kind::Ball)]   > 0);

    std::string why;
    const std::string built = write_replay(r, &why);
    CHECK(!built.empty());

    const auto ondisk = assets::load_file(kReferencePath);
    CHECK(ondisk.has_value());
    if (!ondisk) return;
    const std::string committed(ondisk->begin(), ondisk->end());

    if (committed != built) {
        std::printf("FAIL %s differs from what this machine computes.\n", kReferencePath);
        std::printf("     Either the tables moved (then re-bake:\n");
        std::printf("       ./build/demo --cmd creature.record %s )\n", kReferencePath);
        std::printf("     or this machine disagrees with the one that recorded it,\n");
        std::printf("     which is the bug this file exists to catch.\n");
        ++g_failures;
    }

    // ...and it must still verify, which is a different question from byte equality:
    // a file could match and the verifier still be broken.
    Replay back;
    CHECK(read_replay(d, committed, back, &why));
    const Verdict v = verify(d, back);
    CHECK(v.ok);
    CHECK(v.fault == Verdict::Fault::None);
}

// -----------------------------------------------------------------------------
// What the format REFUSES. Every branch here is a file that would otherwise be
// read into a battle nobody played.
// -----------------------------------------------------------------------------
void test_the_format_refuses(const Dex& d) {
    const Replay r = reference_battle(d);
    std::string why;
    const std::string good = write_replay(r, &why);
    CHECK(!good.empty());

    Replay back;
    CHECK(read_replay(d, good, back, &why));
    CHECK(back.rules == r.rules);
    CHECK(back.turns.size() == r.turns.size());
    // Round trip, twice: writing what was read must produce the same bytes, or the
    // reader is quietly dropping something the writer puts back by luck.
    CHECK(write_replay(back, &why) == good);

    struct Case { const char* what; std::string text; };
    const std::string future = "crep" + std::to_string(kReplayVersion + 1) + good.substr(5);
    std::vector<Case> bad = {
        {"empty",            ""},
        {"a version from the future", future},
        {"no magic",         good.substr(6)},
        {"an unknown record", good + "colour blue\n"},
        // A COMPLETE second declaration. The first version of this case appended a
        // bare `side 0 3 0` and was refused for being three creatures short — so it
        // passed with the duplicate-guard deleted, which is a case that tests the
        // wrong guard and reads exactly like one that tests the right one.
        {"a side declared twice",
         good + "side 0 1 0\nc 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n"},
        {"a move slot one past the end",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 1 0\n"
                     "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\nside 1 1 0\n"
                     "c 4 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n"
                     "t 0 4 0 0 0000000000000000\n")},
        {"a ball with no bonus at all",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 1 0\n"
                     "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\nside 1 1 0\n"
                     "c 4 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n"
                     "t 3 0 0 0 0000000000000000\n")},
        // A FULL party plus one. The count check at the end refuses this file either
        // way; what `p.count >= want` prevents is the write to `member[6]` that
        // happens first. That makes it a memory guard rather than a behaviour one,
        // and no assertion here can see the difference — the ASan build can, and
        // that is what the chapter records.
        {"a seventh creature in a party of six",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 6 0\n") +
             "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n" "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n" "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n"
             "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n" "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n" "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n" "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n"
             "side 1 1 0\nc 4 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n"},
        {"one creature more than the side declared",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 1 0\n"
                     "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n"
                     "c 2 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\nside 1 1 0\n"
                     "c 4 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n")},
        {"a side short of what it declared",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 3 0\n"
                     "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\nside 1 1 0\n"
                     "c 4 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n")},
        {"a level nobody can reach",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 1 0\n"
                     "c 1 500 0 20 0 0 0 35 -1 0 -1 0 -1 0\nside 1 1 0\n"
                     "c 4 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n")},
        {"a move slot out of range",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 1 0\n"
                     "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\nside 1 1 0\n"
                     "c 4 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n"
                     "t 0 9 0 0 0000000000000000\n")},
        {"a species nobody declared",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 1 0\n"
                     "c 999 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\nside 1 1 0\n"
                     "c 4 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n")},
        {"an active outside the party",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 1 3\n"
                     "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\nside 1 1 0\n"
                     "c 4 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n")},
        {"a side that never arrived",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 1 0\n"
                     "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n")},
        {"a truncated hash",
         std::string("crep1\nrules 0000000000000000\nseed 1\nside 0 1 0\n"
                     "c 1 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\nside 1 1 0\n"
                     "c 4 5 0 20 0 0 0 35 -1 0 -1 0 -1 0\n"
                     "t 0 0 0 0 abc\n")},
    };
    for (const Case& c : bad) {
        Replay out;
        if (read_replay(d, c.text, out, &why)) {
            std::printf("FAIL the format accepted %s\n", c.what);
            ++g_failures;
        }
    }

    // A DAMAGED start. Every replay above begins with two fresh parties, and a fresh
    // party is at full HP — so a reader that ignored the stored `hp` and filled every
    // creature to `max_hp` would round-trip all of them perfectly. That is not a
    // hypothetical: the game's own recordings always start damaged, because the party
    // walks into the grass carrying whatever the last fight left it.
    {
        Replay hurt;
        hurt.rules = rules_hash(d);
        hurt.start.side[0] = make_party(d, {{1, 20}, {5, 18}});
        hurt.start.side[1] = make_party(d, {{4, 19}});
        hurt.start.rng     = 0xD00Dull;
        hurt.start.side[0].member[0].hp = 7;                    // nearly out
        hurt.start.side[0].member[1].hp = 1;
        hurt.start.side[0].member[1].status = Status::Burn;
        hurt.start.side[0].member[1].sleep  = 0;
        hurt.start.side[1].member[0].hp -= 3;
        hurt.start.side[0].member[0].moves[0].pp = 2;           // ...and half out of PP
        hurt.start.side[0].active = 0;

        Battle live = hurt.start;
        for (int t = 0; t < 40 && !live.over; ++t) {
            const Action a0 = choose(d, live, 0), a1 = choose(d, live, 1);
            step(d, live, a0, a1);
            hurt.turns.push_back(Turn{a0, a1, hash(live)});
        }
        CHECK(hurt.turns.size() >= 1);

        std::string w2;
        const std::string text = write_replay(hurt, &w2);
        CHECK(!text.empty());
        Replay in;
        CHECK(read_replay(d, text, in, &w2));
        CHECK(hash(in.start) == hash(hurt.start));              // THE assertion
        CHECK(in.start.side[0].member[0].hp == 7);
        CHECK(in.start.side[0].member[0].moves[0].pp == 2);
        CHECK(in.start.side[0].member[1].status == Status::Burn);
        CHECK(verify(d, in).ok);
    }

    // Every action kind through the format, including the one no AI ever picks.
    // `Run` ends the battle on the turn it is chosen, so it can only be last.
    {
        Replay all;
        all.rules = rules_hash(d);
        all.start.side[0] = make_party(d, {{1, 20}, {5, 20}});
        all.start.side[1] = make_party(d, {{4, 20}, {8, 20}});
        all.start.rng     = 0xBEEFull;
        const Action script[] = {
            Action{Action::Kind::Move,   0},
            Action{Action::Kind::Switch, 1},
            Action{Action::Kind::Ball,   250},
            Action{Action::Kind::Run,    0},
        };
        Battle live = all.start;
        for (const Action& a0 : script) {
            if (live.over) break;
            const Action a1 = choose(d, live, 1);
            step(d, live, a0, a1);
            all.turns.push_back(Turn{a0, a1, hash(live)});
        }
        CHECK(all.turns.size() == 4);
        std::string w3;
        const std::string text = write_replay(all, &w3);
        CHECK(!text.empty());
        Replay in;
        CHECK(read_replay(d, text, in, &w3));
        CHECK(in.turns.size() == 4);
        for (std::size_t i = 0; i < 4; ++i) {
            CHECK(in.turns[i].a0.kind  == script[i].kind);
            CHECK(in.turns[i].a0.index == script[i].index);     // 250 is not 100
        }
        CHECK(verify(d, in).ok);
        CHECK(play(d, in).fled);

        // ...and turns recorded AFTER the battle ended are skipped rather than
        // replayed into it. A tape from a network peer can carry them; a verifier
        // that stepped anyway would compare a state nobody produced.
        Replay tail = in;
        tail.turns.push_back(Turn{Action{Action::Kind::Move, 0},
                                  Action{Action::Kind::Move, 0}, 0xDEADBEEFull});
        const Verdict v = verify(d, tail);
        CHECK(v.ok);
        CHECK(v.turn == 0);
    }

    // ...and the writer's own refusal, which has to be checked in BOTH directions:
    // a guard that never lets anything through is not a guard (chapter 127).
    Replay midway = r;
    midway.start.turn = 4;
    CHECK(write_replay(midway, &why).empty());
    Replay over = r;
    over.start.over = true;
    CHECK(write_replay(over, &why).empty());
    Replay illegal = r;
    illegal.turns[0].a0 = Action{Action::Kind::Move, 9};
    CHECK(write_replay(illegal, &why).empty());
    CHECK(!write_replay(r, &why).empty());          // the same object, still fine
}

// -----------------------------------------------------------------------------
// The two verdicts a verifier can return, and why they are not the same verdict.
// -----------------------------------------------------------------------------
void test_the_verifier_tells_the_two_faults_apart(const Dex& d) {
    const Replay r = reference_battle(d);
    CHECK(verify(d, r).ok);

    // Somebody re-tuned a move: the rules hash moves, and the verifier must say so
    // rather than shout DESYNC at a perfectly honest machine.
    Replay rebalanced = r;
    rebalanced.rules ^= 1;
    const Verdict a = verify(d, rebalanced);
    CHECK(!a.ok);
    CHECK(a.fault == Verdict::Fault::Rules);
    CHECK(a.turn == 0);

    // A machine that computed a different state: same rules, wrong hash, and the
    // TURN it first went wrong is the answer — not the last turn, where everything
    // differs and nothing is diagnosable.
    Replay wrong = r;
    wrong.turns[3].after ^= 0x40;
    const Verdict b = verify(d, wrong);
    CHECK(!b.ok);
    CHECK(b.fault == Verdict::Fault::Desync);
    CHECK(b.turn == 4);
    CHECK(b.got == r.turns[3].after);

    // The rules hash covers what `step` reads and nothing else. Moving a creature to
    // a different patch of grass must not invalidate a recording of a fight.
    Dex moved = d;
    if (!moved.tables.empty()) {
        moved.tables[0].entries.push_back(EncounterEntry{2, 5, 2, 3});
        CHECK(rules_hash(moved) == rules_hash(d));
        CHECK(verify(moved, r).ok);
    }
    // ...but re-tuning a move IS in it.
    Dex tuned = d;
    CHECK(!tuned.moves.empty());
    tuned.moves[0].power += 1;
    CHECK(rules_hash(tuned) != rules_hash(d));
}

// -----------------------------------------------------------------------------
// Two doors into one calculation. `try_catch` measures the catch odds without a
// battle happening around it; `step` throws the ball as a turn. They must agree
// exactly, or the odds a test measures are not the odds a player faces.
// -----------------------------------------------------------------------------
void test_the_two_catch_doors_agree(const Dex& d) {
    int through_step = 0, through_call = 0;
    for (int seed = 1; seed <= 400; ++seed) {
        Battle a;
        a.side[0] = make_party(d, {{1, 12}});
        a.side[1] = make_party(d, {{4, 10}});
        a.rng     = static_cast<std::uint64_t>(seed) * 2654435761ull + 7;
        Battle b  = a;

        const bool by_call = try_catch(d, a, 1);
        // Through the turn: the ball is thrown first (priority 6), so the roll is the
        // first thing drawn from the same state.
        step(d, b, Action{Action::Kind::Ball, 100}, Action{Action::Kind::Move, 0});

        if (by_call != b.caught) {
            std::printf("FAIL the two catch doors disagreed at seed %d\n", seed);
            ++g_failures;
            break;
        }
        through_call += by_call ? 1 : 0;
        through_step += b.caught ? 1 : 0;
    }
    CHECK(through_call == through_step);
    // Both actually caught something and both actually failed, or the agreement above
    // is the agreement of two functions that always say no.
    CHECK(through_call > 20);
    CHECK(through_call < 380);
}

// -----------------------------------------------------------------------------
// A ball is a turn. It costs the throw whether or not it lands, and the wild side
// keeps its move when it does not — which used to be arranged by telling `step` to
// switch to the slot already active, a lie that could not be recorded.
// -----------------------------------------------------------------------------
void test_a_ball_is_a_turn(const Dex& d) {
    int stuck = 0, freed = 0, wild_acted = 0;
    for (int seed = 1; seed <= 300 && (stuck < 5 || freed < 5); ++seed) {
        Battle b;
        b.side[0] = make_party(d, {{1, 20}});
        b.side[1] = make_party(d, {{4, 5}});
        b.rng     = static_cast<std::uint64_t>(seed) * 6364136223846793005ull + 1;
        const int turn_before = b.turn;

        std::vector<Event> log;
        step(d, b, Action{Action::Kind::Ball, 100}, Action{Action::Kind::Move, 0}, &log);
        CHECK(b.turn == turn_before + 1);

        bool saw_ball = false, saw_used = false;
        int  ball_says = -1;
        for (const Event& e : log) {
            if (e.kind == Event::Kind::Ball) { saw_ball = true; ball_says = e.a; CHECK(e.side == 0); }
            if (e.kind == Event::Kind::Used && e.side == 1) saw_used = true;
        }
        CHECK(saw_ball);
        // The EVENT has to carry the outcome, not just the battle. Events are the
        // core's string-free report — the thing a log, a network frame or a narrator
        // reads — and a flag nobody checks is a flag that can be wrong forever.
        CHECK(ball_says == (b.caught ? 1 : 0));
        if (b.caught) {
            ++stuck;
            CHECK(b.over);
            CHECK(b.winner == 0);
            CHECK(!saw_used);          // it never got to move: the fight was over
        } else {
            ++freed;
            CHECK(!b.over || !b.side[0].any_alive());
            if (saw_used) ++wild_acted;
        }
    }
    CHECK(stuck > 0);
    CHECK(freed > 0);
    CHECK(wild_acted > 0);
    // A bigger ball is a better ball, and that has to be true of the number the
    // FORMAT carries, not just of a constant in the code.
    int weak = 0, strong = 0;
    for (int seed = 1; seed <= 400; ++seed) {
        Battle a, b;
        a.side[0] = b.side[0] = make_party(d, {{1, 20}});
        a.side[1] = b.side[1] = make_party(d, {{4, 5}});
        a.rng = b.rng = static_cast<std::uint64_t>(seed) * 2246822519ull + 3;
        step(d, a, Action{Action::Kind::Ball, 50},  Action{Action::Kind::Move, 0});
        step(d, b, Action{Action::Kind::Ball, 400}, Action{Action::Kind::Move, 0});
        weak   += a.caught ? 1 : 0;
        strong += b.caught ? 1 : 0;
    }
    CHECK(strong > weak);

    // A ball goes FIRST, and the only way to prove that is a fight the thrower is
    // about to lose. A level-3 starter against a fully evolved level-40 is one-shot
    // every time: if the ball did not outrank a move, `act` would reach the throw
    // with the thrower already fainted and there would be no throw at all. Every
    // other test here pairs a fast thrower with a slow target, where a priority of
    // zero produces exactly the same battle.
    {
        int thrown = 0, lost = 0;
        for (int seed = 1; seed <= 60; ++seed) {
            Battle b;
            b.side[0] = make_party(d, {{1, 3}});
            b.side[1] = make_party(d, {{18, 40}});
            b.rng     = static_cast<std::uint64_t>(seed) * 1099511628211ull + 5;
            CHECK(b.side[1].now().spd > b.side[0].now().spd);   // the premise, asserted
            std::vector<Event> log;
            step(d, b, Action{Action::Kind::Ball, 100}, Action{Action::Kind::Move, 0}, &log);
            for (const Event& e : log) if (e.kind == Event::Kind::Ball) ++thrown;
            if (b.over && b.winner == 1) ++lost;
        }
        CHECK(thrown == 60);
        CHECK(lost > 30);          // ...and the fight really was being lost
    }
}

int main() {
    assets::set_base_path(ASSET_ROOT "/assets");

    const Dex d = load_shipped_dex();
    test_tables(d);
    test_parser_refusals();
    test_validate();
    test_make(d);
    test_turn(d);
    test_status(d);
    test_catch(d);
    test_hash_is_sensitive(d);
    test_ai(d);
    test_rng();
    test_damage_modifiers(d);
    test_the_coin_is_a_coin(d);
    test_accuracy_and_paralysis(d);
    test_the_survivor_wins(d);
    test_type_chart_survives_growth();
    test_no_free_lunch(d);
    test_every_species_wears_a_sprite(d);
    test_evolution_is_one_more_part(d);
    test_thousand_replays(d);
    test_a_ball_is_a_turn(d);
    test_the_two_catch_doors_agree(d);
    test_the_format_refuses(d);
    test_the_verifier_tells_the_two_faults_apart(d);
    test_the_reference_battle_is_bytes(d);

    if (g_failures == 0) std::printf("test_creature: all checks passed\n");
    else                 std::printf("test_creature: %d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
