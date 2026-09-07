// =============================================================================
//  games/creatures/world.cpp
// =============================================================================
#include "games/creatures/world.hpp"

#include <algorithm>
#include <sstream>

namespace creature {

namespace {

// Re-derive a creature's stats for its current species and level, KEEPING the damage
// it has taken. Every growth path goes through here — a level, an evolution — so
// there is one place that can be wrong about it.
//
// Keeping damage is the load-bearing half. A level-up that refilled HP would end
// every fight the moment anything levelled, and an evolution that did it would make
// evolving a heal.
void regrow(const Dex& d, Creature& c) {
    const SpeciesDef* s = d.species_by_id(c.species);
    if (!s) return;
    const int lost = c.max_hp - c.hp;
    c.max_hp = (s->hp  * 2 * c.level) / 100 + c.level + 10;
    c.atk    = (s->atk * 2 * c.level) / 100 + 5;
    c.def    = (s->def * 2 * c.level) / 100 + 5;
    c.spd    = (s->spd * 2 * c.level) / 100 + 5;
    c.hp     = std::max(1, c.max_hp - lost);
}

// Every move the species can know at this level, newest last.
void teach(const Dex& d, Creature& c, int at_level, std::vector<int>* learned) {
    const SpeciesDef* s = d.species_by_id(c.species);
    if (!s) return;
    for (const auto& [lvl, name] : s->learnset) {
        if (lvl != at_level) continue;
        const int mi = d.move_index(name);
        if (mi < 0) continue;
        bool known = false;
        for (int i = 0; i < kMoveSlots; ++i) if (c.moves[i].move == mi) known = true;
        if (known) continue;
        int slot = -1;
        for (int i = 0; i < kMoveSlots && slot < 0; ++i) if (c.moves[i].move < 0) slot = i;
        if (slot < 0) {
            // Full: the oldest goes. Same rule as `make`, which keeps the LAST four.
            for (int i = 0; i + 1 < kMoveSlots; ++i) c.moves[i] = c.moves[i + 1];
            slot = kMoveSlots - 1;
        }
        c.moves[slot] = MoveSlot{mi, d.move(mi)->pp};
        if (learned) learned->push_back(mi);
    }
}

int tile_at(const tilemap::Map& m, const std::string& layer, int x, int y) {
    return static_cast<int>(m.at(layer, x, y));
}

} // namespace

int exp_to_next(int level) { return std::max(1, level * level * 2); }

World new_game(const Dex& d, int starter, int x, int y, std::uint64_t seed) {
    World w;
    w.px = w.home_x = x;
    w.py = w.home_y = y;
    w.party = make_party(d, {{starter, 5}});
    w.rng   = seed ? seed : 1;
    return w;
}

// ---- the overworld --------------------------------------------------------------

WalkResult walk(const Dex& d, World& w, const tilemap::Map& m, int dx, int dy) {
    WalkResult r;
    if (w.phase != Phase::Overworld) return r;          // the phase is the only gate
    if ((dx == 0) == (dy == 0)) return r;               // one axis, one tile

    if (dx > 0)      w.facing = 1;
    else if (dx < 0) w.facing = 3;
    else if (dy > 0) w.facing = 2;
    else             w.facing = 0;

    const int nx = w.px + dx, ny = w.py + dy;
    if (nx < 0 || ny < 0 || nx >= m.w || ny >= m.h) { r.blocked = true; return r; }
    if (tile_at(m, "collide", nx, ny) != 0)         { r.blocked = true; return r; }

    w.px = nx;
    w.py = ny;
    r.moved = true;

    if (tile_at(m, "ground", nx, ny) != kLongGrass) return r;

    engine::Rng rng(w.rng);
    const bool ambush = rng.range(1, 100) <= kEncounterPct;
    // Which TABLE is a property of the tile, and the route has two bands: the near
    // grass is gentle, the far grass is everything. It is a MASK LAYER rather than a
    // second ground id, so the two patches look identical and the player finds out by
    // walking into one — and, more to the point, so the map keeps saying it. A band
    // encoded as "x > 20" inside this file would be a fact about the world living in
    // the code, which is the mistake chapter 134 already fixed once.
    const char* table = tile_at(m, "far", nx, ny) != 0 ? "far" : "near";
    int species = 0, level = 0;
    if (ambush) {
        const EncounterTable* t = d.table(table);
        if (t && t->total_weight() > 0) {
            int pick = rng.range(1, t->total_weight());
            for (const EncounterEntry& e : t->entries) {
                pick -= e.weight;
                if (pick <= 0) { species = e.species; level = rng.range(e.lo, e.hi); break; }
            }
        }
    }
    w.rng = rng.state();

    if (species > 0) {
        begin_battle(d, w, species, level);
        r.encounter = true;
    }
    return r;
}

// ---- the fight ------------------------------------------------------------------

void begin_battle(const Dex& d, World& w, int species, int level) {
    // A party whose lead has fainted must not walk into a fight with it out front.
    const int lead = w.party.first_alive();
    if (lead < 0) { w.phase = Phase::Blackout; return; }
    w.party.active = lead;

    w.battle = Battle{};
    w.battle.side[0] = w.party;
    w.battle.side[1] = make_party(d, {{species, level}});
    w.battle.rng     = w.rng;
    w.tape           = Replay{};
    w.tape.rules     = rules_hash(d);
    w.tape.start     = w.battle;
    w.wild_species   = species;
    w.wild_level     = level;
    w.log.clear();
    w.phase = Phase::Battle;
}

namespace {

// After any turn: pull the party back out of the battle, hand out experience, and
// decide whether the fight is over. One place, called by both the move path and the
// ball path, because "who won" answered twice is how a game gets two answers.
void settle(const Dex& d, World& w) {
    w.party = w.battle.side[0];
    w.rng   = w.battle.rng;
    if (!w.battle.over) return;

    // Caught is checked BEFORE winner: a ball that stuck sets `winner = 0`, and the
    // branch below would read that as a knockout — award experience for a creature
    // that never fainted and call the fight Won.
    if (w.battle.caught) {
        if (w.party.count < kPartySize) {
            w.party.member[w.party.count++] = w.battle.side[1].now();
            ++w.caught;
            w.phase = Phase::Caught;
        } else {
            // A full party means the throw worked and the creature goes nowhere. It
            // still ends the fight — the alternative is a ball that vanishes and a
            // battle that continues, which reads as a bug.
            w.phase = Phase::Fled;
        }
        return;
    }
    if (w.battle.fled)              { w.phase = Phase::Fled; return; }
    if (w.battle.winner == 1)       { w.phase = Phase::Blackout; return; }
    if (w.battle.winner == 0) {
        ++w.wins;
        // The defeated creature's level is what it is worth. Deliberately not its
        // stat total: a table being balanced changes stats constantly, and experience
        // that moved with them would re-balance progression by accident.
        award_exp(d, w, w.wild_level * 8);
        w.phase = Phase::Won;
        return;
    }
    w.phase = Phase::Fled;          // a double knockout: nothing to award
}

} // namespace

void battle_turn(const Dex& d, World& w, Action player) {
    if (w.phase != Phase::Battle) return;
    w.log.clear();
    // The wild side's choice is named rather than inlined into `step`, because the
    // recording needs BOTH actions. `choose` is deliberately free of the battle's
    // rng (see battle.hpp), so naming it here changes nothing about the stream.
    const Action wild = choose(d, w.battle, 1);
    step(d, w.battle, player, wild, &w.log);
    w.tape.turns.push_back(Turn{player, wild, hash(w.battle)});
    settle(d, w);
}

bool throw_ball(const Dex& d, World& w) {
    if (w.phase != Phase::Battle || w.balls <= 0) return false;
    --w.balls;

    // One line, because a ball is now a turn. What this used to be — a catch resolved
    // beside `step`, and, when it failed, a SWITCH TO THE SLOT ALREADY ACTIVE so that
    // `step` would give the wild side its move — was a lie told to the resolver to
    // buy a side effect. It also meant the throw never appeared in the list of
    // actions, so no replay of this game could contain one (chapter 138).
    battle_turn(d, w, Action{Action::Kind::Ball, 100});
    return w.battle.caught;
}

void end_battle(const Dex& d, World& w) {
    if (w.phase == Phase::Overworld || w.phase == Phase::Battle) return;
    if (w.phase == Phase::Blackout) {
        for (int i = 0; i < kPartySize; ++i) {
            Creature& c = w.party.member[i];
            if (c.species == 0) continue;
            c.hp     = c.max_hp;
            c.status = Status::None;
            c.sleep  = 0;
            for (int m = 0; m < kMoveSlots; ++m) {
                const MoveDef* mv = d.move(c.moves[m].move);
                if (mv) c.moves[m].pp = mv->pp;
            }
        }
        w.px = w.home_x;
        w.py = w.home_y;
    }
    const int lead = w.party.first_alive();
    w.party.active = lead < 0 ? 0 : lead;
    w.phase = Phase::Overworld;
    w.log.clear();
}

// ---- growing --------------------------------------------------------------------

Growth award_exp(const Dex& d, World& w, int amount) {
    Growth g;
    Creature& c = w.party.now();
    if (c.species == 0 || amount <= 0) return g;

    c.exp += amount;
    while (c.exp >= exp_to_next(c.level) && c.level < 100) {
        c.exp -= exp_to_next(c.level);
        ++c.level;
        ++g.levels_gained;
        regrow(d, c);
        teach(d, c, c.level, &g.learned);

        const SpeciesDef* s = d.species_by_id(c.species);
        if (s && s->evolve_to != 0 && c.level >= s->evolve_at) {
            // FIRST and LAST across the whole award, not the last hop. One call can
            // cross two evolutions (a level-5 starter handed a big lump ends up a
            // stage three), and "blazehound became pyrewolf" is not the sentence the
            // player wants — they never had a blazehound.
            if (g.evolved_from == 0) g.evolved_from = c.species;
            g.evolved_to = s->evolve_to;
            c.species    = s->evolve_to;
            regrow(d, c);           // bigger, and still carrying the same wounds
        }
    }
    // A creature at the cap keeps no leftover, or it would bank a level it can never
    // spend and look like the counter is broken.
    if (c.level >= 100) c.exp = 0;
    return g;
}

// ---- the save -------------------------------------------------------------------

std::string to_text(const World& w) {
    std::ostringstream o;
    o << "creaturesave" << kSaveVersion << "\n";
    o << "pos " << w.px << ' ' << w.py << ' ' << w.facing << "\n";
    o << "home " << w.home_x << ' ' << w.home_y << "\n";
    o << "rng " << w.rng << "\n";
    o << "bag " << w.balls << ' ' << w.wins << ' ' << w.caught << "\n";
    o << "party " << w.party.count << ' ' << w.party.active << "\n";
    for (int i = 0; i < w.party.count; ++i) {
        const Creature& c = w.party.member[i];
        o << "c " << c.species << ' ' << c.level << ' ' << c.exp << ' ' << c.hp << ' '
          << static_cast<int>(c.status) << ' ' << c.sleep;
        for (int m = 0; m < kMoveSlots; ++m) o << ' ' << c.moves[m].move << ' ' << c.moves[m].pp;
        o << "\n";
    }
    return o.str();
}

bool from_text(const Dex& d, const std::string& text, World& out) {
    World w;
    std::istringstream in(text);
    std::string line;
    bool have_magic = false;
    int  want = 0;

    while (std::getline(in, line)) {
        std::istringstream ln(line);
        std::string kind;
        if (!(ln >> kind)) continue;

        if (!have_magic) {
            // A version from the future is REFUSED rather than half-read, the same
            // rule map2 has: an old reader that skips fields it does not know writes
            // that loss back to disk the next time it saves.
            if (kind != "creaturesave" + std::to_string(kSaveVersion)) return false;
            have_magic = true;
            continue;
        }
        if (kind == "pos")        { if (!(ln >> w.px >> w.py >> w.facing)) return false; }
        else if (kind == "home")  { if (!(ln >> w.home_x >> w.home_y)) return false; }
        else if (kind == "rng")   { if (!(ln >> w.rng)) return false; }
        else if (kind == "bag")   { if (!(ln >> w.balls >> w.wins >> w.caught)) return false; }
        else if (kind == "party") { if (!(ln >> want >> w.party.active)) return false;
                                    if (want < 0 || want > kPartySize) return false; }
        else if (kind == "c") {
            if (w.party.count >= kPartySize) return false;
            Creature c;
            int status = 0;
            if (!(ln >> c.species >> c.level >> c.exp >> c.hp >> status >> c.sleep)) return false;
            if (!d.species_by_id(c.species)) return false;     // a save naming nothing
            if (c.level < 1 || c.level > 100 || status < 0 || status > 3) return false;
            c.status = static_cast<Status>(status);
            for (int m = 0; m < kMoveSlots; ++m)
                if (!(ln >> c.moves[m].move >> c.moves[m].pp)) return false;
            // Stats are DERIVED, never stored: they are a pure function of species and
            // level, and storing them would let a save disagree with the table it was
            // balanced against — a re-balance would silently not apply to old saves.
            const int hp = c.hp;
            c.hp = c.max_hp = 0;
            regrow(d, c);
            c.hp = std::clamp(hp, 0, c.max_hp);
            w.party.member[w.party.count++] = c;
        }
        else return false;                                     // an unknown record
    }
    if (!have_magic || w.party.count != want || want == 0) return false;
    if (w.party.active < 0 || w.party.active >= w.party.count) return false;
    if (w.balls < 0 || w.balls > kMaxBalls) return false;
    out = w;
    return true;
}

} // namespace creature
