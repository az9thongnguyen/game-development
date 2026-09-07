// =============================================================================
//  games/creatures/battle.cpp
// =============================================================================
#include "games/creatures/battle.hpp"

#include <algorithm>

namespace creature {

namespace {

// A single narrow place where the damage arithmetic lives, so there is exactly one
// version of it to be wrong. `long long` throughout: at level 100 with a 95-power
// move the intermediate passes 2^31 on the way, and an overflow here would be a
// desync that only shows up in the endgame.
long long damage_of(const Dex& d, const MoveDef& mv, const Creature& atk,
                    const Creature& def, int type_mult, int roll) {
    long long base = (2LL * atk.level) / 5 + 2;
    base = base * mv.power * atk.atk / std::max(1, def.def);
    base = base / 50 + 2;

    const SpeciesDef* as = d.species_by_id(atk.species);
    if (as && as->type == mv.type) base = base * 150 / 100;      // STAB

    base = base * type_mult / 100;
    if (atk.status == Status::Burn) base = base / 2;
    base = base * roll / 100;

    if (type_mult > 0 && base < 1) base = 1;   // a hit that connects always stings
    if (type_mult == 0) base = 0;
    return base;
}

void emit(std::vector<Event>* out, Event::Kind k, int side, int a = 0, int b = 0) {
    if (out) out->push_back(Event{k, side, a, b});
}

int defender_type(const Dex& d, const Creature& c) {
    const SpeciesDef* s = d.species_by_id(c.species);
    return s ? s->type : 0;
}

// Paralysis halves speed. It is applied HERE and not stored, because a status that
// edited the stat would have to un-edit it on cure, and "un-edit" is where the
// arithmetic drifts.
int effective_speed(const Creature& c) {
    return c.status == Status::Paralyze ? c.spd / 2 : c.spd;
}

int priority_of(const Dex& d, const Party& p, Action a) {
    if (a.kind == Action::Kind::Run)    return 7;
    if (a.kind == Action::Kind::Switch) return 6;
    const Creature& c = p.now();
    if (a.index < 0 || a.index >= kMoveSlots) return 0;
    const MoveDef* mv = d.move(c.moves[a.index].move);
    return mv ? mv->priority : 0;
}

void feed(std::uint64_t& h, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (i * 8)) & 0xFFull;
        h *= 0x100000001B3ull;
    }
}

} // namespace

// ---- party ---------------------------------------------------------------------

bool Party::any_alive() const {
    for (int i = 0; i < kPartySize; ++i)
        if (member[i].alive()) return true;
    return false;
}

int Party::first_alive(int except) const {
    for (int i = 0; i < kPartySize; ++i)
        if (i != except && member[i].alive()) return i;
    return -1;
}

// ---- building ------------------------------------------------------------------

Creature make(const Dex& d, int species_id, int level) {
    Creature c;
    const SpeciesDef* s = d.species_by_id(species_id);
    if (!s) return c;                       // species 0: an empty slot, not a crash
    c.species = s->id;
    c.level   = std::max(1, level);
    c.max_hp  = (s->hp  * 2 * c.level) / 100 + c.level + 10;
    c.atk     = (s->atk * 2 * c.level) / 100 + 5;
    c.def     = (s->def * 2 * c.level) / 100 + 5;
    c.spd     = (s->spd * 2 * c.level) / 100 + 5;
    c.hp      = c.max_hp;

    // The LAST four moves it is eligible for. Learning is therefore a decision made
    // for the player by the table, which is the whole reason a learnset is ordered.
    int filled = 0;
    for (const auto& [lvl, name] : s->learnset) {
        if (lvl > c.level) continue;
        const int mi = d.move_index(name);
        if (mi < 0) continue;
        if (filled < kMoveSlots) {
            c.moves[filled] = MoveSlot{mi, d.move(mi)->pp};
            ++filled;
        } else {
            for (int i = 0; i + 1 < kMoveSlots; ++i) c.moves[i] = c.moves[i + 1];
            c.moves[kMoveSlots - 1] = MoveSlot{mi, d.move(mi)->pp};
        }
    }
    return c;
}

Party make_party(const Dex& d, const std::vector<std::pair<int, int>>& id_level) {
    Party p;
    for (const auto& [id, lvl] : id_level) {
        if (p.count >= kPartySize) break;
        Creature c = make(d, id, lvl);
        if (c.species == 0) continue;
        p.member[p.count++] = c;
    }
    p.active = p.first_alive();
    if (p.active < 0) p.active = 0;
    return p;
}

// ---- one turn ------------------------------------------------------------------

namespace {

// Resolve one side's action. `rng` is the battle's own stream — every draw here is
// part of the replay.
void act(const Dex& d, Battle& b, engine::Rng& rng, int side, Action a,
         std::vector<Event>* out) {
    const int other = 1 - side;
    Creature& me = b.side[side].now();
    if (!me.alive() || b.over) return;

    if (a.kind == Action::Kind::Run) {
        b.over = true; b.fled = true; b.winner = other;
        emit(out, Event::Kind::Fled, side);
        return;
    }
    if (a.kind == Action::Kind::Switch) {
        // An illegal switch is a wasted turn, not an error: on a network the other
        // player picked it, and rejecting it would need a round trip to say so.
        if (a.index >= 0 && a.index < kPartySize && a.index != b.side[side].active &&
            b.side[side].member[a.index].alive()) {
            b.side[side].active = a.index;
            emit(out, Event::Kind::Switched, side, a.index);
        }
        return;
    }

    if (a.index < 0 || a.index >= kMoveSlots) return;
    MoveSlot& slot = me.moves[a.index];
    const MoveDef* mv = d.move(slot.move);
    if (!mv) return;

    // ---- status gates, before PP is spent ----
    if (me.status == Status::Sleep) {
        if (me.sleep > 0) --me.sleep;
        if (me.sleep == 0) {
            me.status = Status::None;
            emit(out, Event::Kind::WokeUp, side);
        } else {
            emit(out, Event::Kind::Immobilised, side, static_cast<int>(Status::Sleep));
        }
        return;                              // the turn is lost either way
    }
    if (me.status == Status::Paralyze && rng.range(1, 100) <= 25) {
        emit(out, Event::Kind::Immobilised, side, static_cast<int>(Status::Paralyze));
        return;
    }
    if (slot.pp <= 0) {
        emit(out, Event::Kind::NoPP, side, a.index);
        return;
    }

    --slot.pp;
    emit(out, Event::Kind::Used, side, slot.move);

    if (rng.range(1, 100) > mv->acc) {
        emit(out, Event::Kind::Missed, side, slot.move);
        return;
    }

    Creature& target = b.side[other].now();
    const int mult = d.types.multiplier(mv->type, defender_type(d, target));

    if (mv->power > 0) {
        // The roll is drawn whatever the multiplier turns out to be, so the stream
        // does not depend on the type chart. Changing a balance number must not
        // change an old replay.
        const int roll = rng.range(85, 100);
        const int dmg  = static_cast<int>(damage_of(d, *mv, me, target, mult, roll));
        target.hp = std::max(0, target.hp - dmg);
        emit(out, Event::Kind::Damage, other, dmg, mult);
        if (target.hp == 0) emit(out, Event::Kind::Fainted, other);
    }

    if (mv->effect != Effect::None) {
        const int roll = rng.range(1, 100);
        const Status s = static_cast<Status>(mv->effect);
        if (roll <= mv->effect_chance && target.alive() && target.status == Status::None) {
            target.status = s;
            if (s == Status::Sleep) target.sleep = rng.range(1, 3);
            emit(out, Event::Kind::Inflicted, other, static_cast<int>(s));
        }
    }
}

} // namespace

void step(const Dex& d, Battle& b, Action a0, Action a1, std::vector<Event>* out) {
    if (b.over) return;
    ++b.turn;

    engine::Rng rng(b.rng);

    // ---- who goes first: priority, then speed, then the battle's own coin --------
    const int p0 = priority_of(d, b.side[0], a0);
    const int p1 = priority_of(d, b.side[1], a1);
    int first;
    if (p0 != p1) {
        first = p0 > p1 ? 0 : 1;
    } else {
        const int s0 = effective_speed(b.side[0].now());
        const int s1 = effective_speed(b.side[1].now());
        first = s0 != s1 ? (s0 > s1 ? 0 : 1) : rng.range(0, 1);
    }

    act(d, b, rng, first, first == 0 ? a0 : a1, out);
    act(d, b, rng, 1 - first, first == 0 ? a1 : a0, out);

    // ---- end of turn: burn ticks, in a fixed side order -------------------------
    if (!b.over) {
        for (int s = 0; s < 2; ++s) {
            Creature& c = b.side[s].now();
            if (!c.alive() || c.status != Status::Burn) continue;
            const int dmg = std::max(1, c.max_hp / 16);
            c.hp = std::max(0, c.hp - dmg);
            emit(out, Event::Kind::StatusHurt, s, dmg, static_cast<int>(Status::Burn));
            if (c.hp == 0) emit(out, Event::Kind::Fainted, s);
        }
    }

    // ---- fainted actives are replaced; an empty bench ends it -------------------
    if (!b.over) {
        const bool live0 = b.side[0].any_alive();
        const bool live1 = b.side[1].any_alive();
        if (!live0 || !live1) {
            b.over   = true;
            b.winner = (live0 == live1) ? -1 : (live0 ? 0 : 1);
            emit(out, Event::Kind::Win, b.winner);
        } else {
            for (int s = 0; s < 2; ++s) {
                if (b.side[s].now().alive()) continue;
                const int next = b.side[s].first_alive();
                b.side[s].active = next;
                emit(out, Event::Kind::Switched, s, next);
            }
        }
    }

    b.rng = rng.state();
}

std::uint64_t hash(const Battle& b) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (int s = 0; s < 2; ++s) {
        feed(h, static_cast<std::uint64_t>(b.side[s].count));
        feed(h, static_cast<std::uint64_t>(b.side[s].active));
        for (int i = 0; i < kPartySize; ++i) {
            const Creature& c = b.side[s].member[i];
            feed(h, static_cast<std::uint64_t>(c.species));
            feed(h, static_cast<std::uint64_t>(c.level));
            feed(h, static_cast<std::uint64_t>(c.exp));
            feed(h, static_cast<std::uint64_t>(c.hp));
            feed(h, static_cast<std::uint64_t>(c.max_hp));
            feed(h, static_cast<std::uint64_t>(c.atk));
            feed(h, static_cast<std::uint64_t>(c.def));
            feed(h, static_cast<std::uint64_t>(c.spd));
            feed(h, static_cast<std::uint64_t>(c.status));
            feed(h, static_cast<std::uint64_t>(c.sleep));
            for (int m = 0; m < kMoveSlots; ++m) {
                feed(h, static_cast<std::uint64_t>(c.moves[m].move));
                feed(h, static_cast<std::uint64_t>(c.moves[m].pp));
            }
        }
    }
    feed(h, b.rng);
    feed(h, static_cast<std::uint64_t>(b.turn));
    feed(h, static_cast<std::uint64_t>(b.over ? 1 : 0));
    feed(h, static_cast<std::uint64_t>(b.winner));
    feed(h, static_cast<std::uint64_t>(b.fled ? 1 : 0));
    return h;
}

Battle play(const Dex& d, const Replay& r) {
    Battle b = r.start;
    for (const auto& [a0, a1] : r.turns) {
        if (b.over) break;
        step(d, b, a0, a1, nullptr);
    }
    return b;
}

// ---- the opponent --------------------------------------------------------------

Action choose(const Dex& d, const Battle& b, int side) {
    const Party& me  = b.side[side];
    const Party& you = b.side[1 - side];
    const Creature& c = me.now();
    const Creature& t = you.now();
    const int their_type = defender_type(d, t);

    // Switch out when nearly dead AND the bench actually improves the matchup. The
    // second half matters: without it the AI runs away from a fight it is winning.
    if (c.alive() && c.hp * 5 <= c.max_hp) {
        const SpeciesDef* mine = d.species_by_id(c.species);
        const int here = mine ? d.types.multiplier(mine->type, their_type) : kNeutral;
        for (int i = 0; i < kPartySize; ++i) {
            if (i == me.active || !me.member[i].alive()) continue;
            const SpeciesDef* s = d.species_by_id(me.member[i].species);
            if (s && d.types.multiplier(s->type, their_type) > here)
                return Action{Action::Kind::Switch, i};
        }
    }

    int best = -1;
    long long best_score = -1;
    for (int i = 0; i < kMoveSlots; ++i) {
        const MoveSlot& slot = c.moves[i];
        const MoveDef* mv = d.move(slot.move);
        if (!mv || slot.pp <= 0) continue;
        const SpeciesDef* mine = d.species_by_id(c.species);
        long long score = mv->power;
        if (mine && mine->type == mv->type) score = score * 150 / 100;
        score = score * d.types.multiplier(mv->type, their_type) / 100;
        score = score * mv->acc / 100;
        // A status move scores 1 rather than 0, so a creature holding nothing but
        // sleep powder still acts instead of standing there.
        if (mv->power == 0) score = 1;
        if (score > best_score) { best_score = score; best = i; }
    }
    if (best >= 0) return Action{Action::Kind::Move, best};

    const int bench = me.first_alive(me.active);
    if (bench >= 0) return Action{Action::Kind::Switch, bench};
    return Action{Action::Kind::Move, 0};
}

// ---- catching ------------------------------------------------------------------

bool try_catch(const Dex& d, Battle& b, int side, int ball_bonus) {
    Creature& t = b.side[side].now();
    if (b.over || !t.alive()) return false;

    const SpeciesDef* s = d.species_by_id(t.species);
    const int rate = s ? s->catch_rate : 190;

    long long a = (3LL * t.max_hp - 2LL * t.hp) * rate * ball_bonus /
                  (3LL * std::max(1, t.max_hp) * 100);
    if (t.status == Status::Sleep)      a = a * 2;
    else if (t.status != Status::None)  a = a * 3 / 2;
    a = std::clamp<long long>(a, 0, 255);

    engine::Rng rng(b.rng);
    const bool caught = rng.range(0, 255) < a;
    b.rng = rng.state();

    if (caught) {
        b.over   = true;
        b.winner = 1 - side;
    }
    return caught;
}

} // namespace creature
