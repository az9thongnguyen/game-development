// =============================================================================
//  games/creatures/replay.cpp  —  see replay.hpp
// =============================================================================
#include "games/creatures/replay.hpp"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace creature {
namespace {

constexpr const char* kMagic = "crep";

} // namespace

std::string hex16(std::uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
    return buf;
}

bool parse_hex16(const std::string& s, std::uint64_t& out) {
    if (s.size() != 16) return false;
    std::uint64_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9')      d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else return false;
        v = (v << 4) | static_cast<std::uint64_t>(d);
    }
    out = v;
    return true;
}

namespace {

bool fail(std::string* why, std::string msg) {
    if (why) *why = std::move(msg);
    return false;
}

// The index range an action of this kind may legally carry. The SIM is deliberately
// tolerant here — an out-of-range switch is a wasted turn, because on a network the
// other player chose it and rejecting it would cost a round trip to say so. A FILE
// is not a network: nothing had to be tolerated to receive it, so a nonsense action
// in one means the file is wrong, and reading it anyway would produce a battle the
// recorder never played.
bool legal_action(Action a) {
    switch (a.kind) {
        case Action::Kind::Move:   return a.index >= 0 && a.index < kMoveSlots;
        case Action::Kind::Switch: return a.index >= 0 && a.index < kPartySize;
        case Action::Kind::Run:    return a.index == 0;
        case Action::Kind::Ball:   return a.index > 0 && a.index <= 1000;
    }
    return false;
}

void write_creature(std::ostringstream& o, const Creature& c) {
    o << "c " << c.species << ' ' << c.level << ' ' << c.exp << ' ' << c.hp << ' '
      << static_cast<int>(c.status) << ' ' << c.sleep;
    for (int m = 0; m < kMoveSlots; ++m) o << ' ' << c.moves[m].move << ' ' << c.moves[m].pp;
    o << "\n";
}

// Stats are DERIVED from species and level, exactly as the save does it (world.cpp),
// and for the same reason: a file that stored them could disagree with the table it
// was balanced against, and then a re-balance would silently not reach old files.
bool read_creature(const Dex& d, std::istringstream& ln, Creature& out) {
    int species = 0, level = 0, exp = 0, hp = 0, status = 0, sleep = 0;
    if (!(ln >> species >> level >> exp >> hp >> status >> sleep)) return false;
    if (!d.species_by_id(species)) return false;
    if (level < 1 || level > 100 || status < 0 || status > 3 || sleep < 0) return false;

    Creature c = make(d, species, level);
    c.exp    = exp;
    c.status = static_cast<Status>(status);
    c.sleep  = sleep;
    for (int m = 0; m < kMoveSlots; ++m) {
        int mv = -1, pp = 0;
        if (!(ln >> mv >> pp)) return false;
        if (mv < -1 || mv >= static_cast<int>(d.moves.size()) || pp < 0) return false;
        c.moves[m] = MoveSlot{mv, pp};
    }
    c.hp = std::clamp(hp, 0, c.max_hp);
    out  = c;
    return true;
}

} // namespace

Battle play(const Dex& d, const Replay& r) {
    Battle b = r.start;
    for (const Turn& t : r.turns) {
        if (b.over) break;
        step(d, b, t.a0, t.a1);
    }
    return b;
}

std::string write_replay(const Replay& r, std::string* why) {
    if (r.start.turn != 0 || r.start.over || r.start.fled || r.start.caught) {
        if (why) *why = "a replay must start at turn 0 of a battle that is not over";
        return {};
    }
    if (r.turns.size() > static_cast<std::size_t>(kMaxReplayTurns)) {
        if (why) *why = "too many turns";
        return {};
    }
    for (const Turn& t : r.turns) {
        if (!legal_action(t.a0) || !legal_action(t.a1)) {
            if (why) *why = "a recorded action is out of range";
            return {};
        }
    }

    std::ostringstream o;
    o << kMagic << kReplayVersion << "\n";
    o << "rules " << hex16(r.rules) << "\n";
    o << "seed " << r.start.rng << "\n";
    for (int s = 0; s < 2; ++s) {
        o << "side " << s << ' ' << r.start.side[s].count << ' ' << r.start.side[s].active << "\n";
        for (int i = 0; i < r.start.side[s].count; ++i) write_creature(o, r.start.side[s].member[i]);
    }
    for (const Turn& t : r.turns) {
        o << "t " << static_cast<int>(t.a0.kind) << ' ' << t.a0.index << ' '
          << static_cast<int>(t.a1.kind) << ' ' << t.a1.index << ' ' << hex16(t.after) << "\n";
    }
    return o.str();
}

bool read_replay(const Dex& d, const std::string& text, Replay& out, std::string* why) {
    Replay r;
    std::istringstream in(text);
    std::string line;
    bool have_magic = false, have_rules = false, have_seed = false;
    int  side = -1;        // the side `c` lines are currently filling
    int  want[2] = {0, 0};

    while (std::getline(in, line)) {
        std::istringstream ln(line);
        std::string kind;
        if (!(ln >> kind)) continue;

        if (!have_magic) {
            // A version from the future is refused, never half-read. The same rule
            // map2 and the creature save follow: a reader that skips what it does
            // not understand writes the loss back the next time anything saves.
            if (kind != kMagic + std::to_string(kReplayVersion))
                return fail(why, "not a crep" + std::to_string(kReplayVersion) + " replay");
            have_magic = true;
            continue;
        }

        if (kind == "rules") {
            std::string h;
            if (!(ln >> h) || !parse_hex16(h, r.rules)) return fail(why, "bad rules hash");
            have_rules = true;
        } else if (kind == "seed") {
            if (!(ln >> r.start.rng)) return fail(why, "bad seed");
            have_seed = true;
        } else if (kind == "side") {
            int s = -1, count = 0, active = 0;
            if (!(ln >> s >> count >> active)) return fail(why, "bad side header");
            if (s < 0 || s > 1) return fail(why, "side must be 0 or 1");
            if (want[s] != 0) return fail(why, "side declared twice");
            if (count < 1 || count > kPartySize) return fail(why, "side has no legal party size");
            if (active < 0 || active >= count) return fail(why, "active is outside the party");
            r.start.side[s].count  = 0;      // filled by the `c` lines below
            r.start.side[s].active = active;
            want[s] = count;
            side    = s;
        } else if (kind == "c") {
            if (side < 0) return fail(why, "a creature before its side");
            Party& p = r.start.side[side];
            if (p.count >= want[side]) return fail(why, "more creatures than the side declared");
            if (!read_creature(d, ln, p.member[p.count])) return fail(why, "bad creature");
            ++p.count;
        } else if (kind == "t") {
            if (static_cast<int>(r.turns.size()) >= kMaxReplayTurns)
                return fail(why, "too many turns");
            int k0 = 0, i0 = 0, k1 = 0, i1 = 0;
            std::string h;
            if (!(ln >> k0 >> i0 >> k1 >> i1 >> h)) return fail(why, "bad turn");
            // No range check on the kind: `legal_action` below is the one gate, and a
            // cast of any int to this enum lands on a value its switch answers false
            // for. A second guard here read like belt and braces and was neither —
            // a mutation deleting it changed nothing, which is how it was found.
            Turn t;
            t.a0 = Action{static_cast<Action::Kind>(k0), i0};
            t.a1 = Action{static_cast<Action::Kind>(k1), i1};
            if (!legal_action(t.a0) || !legal_action(t.a1)) return fail(why, "action out of range");
            if (!parse_hex16(h, t.after)) return fail(why, "bad turn hash");
            r.turns.push_back(t);
        } else {
            return fail(why, "unknown record: " + kind);
        }
    }

    if (!have_magic) return fail(why, "empty replay");
    if (!have_rules) return fail(why, "no rules hash");
    if (!have_seed)  return fail(why, "no seed");
    for (int s = 0; s < 2; ++s) {
        if (want[s] == 0) return fail(why, "a side is missing");
        if (r.start.side[s].count != want[s]) return fail(why, "a side is short of creatures");
    }
    out = r;
    return true;
}

Verdict verify(const Dex& d, const Replay& r) {
    Verdict v;
    const std::uint64_t rules = rules_hash(d);
    if (rules != r.rules) {
        // Reported apart from a desync on purpose. "The tables moved" and "the two
        // machines disagree about the arithmetic" are the same symptom and utterly
        // different problems, and a verifier that could not tell them apart would
        // cry wolf every time somebody tuned a move.
        v.fault = Verdict::Fault::Rules;
        v.want  = r.rules;
        v.got   = rules;
        v.why   = "recorded under different rules (" + hex16(r.rules) + " != " + hex16(rules) + ")";
        v.final = r.start;
        return v;
    }

    Battle b = r.start;
    int n = 0;
    for (const Turn& t : r.turns) {
        if (b.over) break;             // a recording may carry turns past the end
        ++n;
        step(d, b, t.a0, t.a1);
        const std::uint64_t h = hash(b);
        if (h != t.after) {
            v.fault = Verdict::Fault::Desync;
            v.turn  = n;
            v.want  = t.after;
            v.got   = h;
            v.final = b;
            v.why   = "turn " + std::to_string(n) + ": " + hex16(t.after) + " != " + hex16(h);
            return v;
        }
    }

    v.ok    = true;
    v.final = b;
    v.why   = "replayed " + std::to_string(n) + " turns";
    return v;
}

Replay reference_battle(const Dex& d) {
    Battle b;
    b.side[0] = make_party(d, {{1, 12}, {5, 14}, {9, 11}});
    b.side[1] = make_party(d, {{4, 13}, {8, 12}, {12, 15}});
    b.rng     = 0x51ED0FC0FFEE1234ull;

    Replay r;
    r.rules = rules_hash(d);
    r.start = b;
    for (int t = 0; t < 200 && !b.over; ++t) {
        // Both sides are the AI, except where the script overrides side 0 — so the
        // file is a function of the tables and nothing else, and re-baking it on
        // another machine cannot accidentally depend on that machine.
        Action a0 = choose(d, b, 0);
        if (t == 2) a0 = Action{Action::Kind::Ball, 100};
        if (t == 5) a0 = Action{Action::Kind::Switch, 1};
        const Action a1 = choose(d, b, 1);
        step(d, b, a0, a1);
        r.turns.push_back(Turn{a0, a1, hash(b)});
    }
    return r;
}

} // namespace creature
