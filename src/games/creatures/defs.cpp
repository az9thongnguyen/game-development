// =============================================================================
//  games/creatures/defs.cpp
// =============================================================================
#include "games/creatures/defs.hpp"

#include <sstream>

namespace creature {

namespace {

bool split_kv(const std::string& tok, std::string& key, std::string& value) {
    const auto eq = tok.find('=');
    if (eq == std::string::npos) return false;
    key   = tok.substr(0, eq);
    value = tok.substr(eq + 1);
    return true;
}

bool to_int(const std::string& s, int& out) {
    if (s.empty()) return false;
    try {
        std::size_t used = 0;
        const int v = std::stoi(s, &used);
        if (used != s.size()) return false;
        out = v;
        return true;
    } catch (...) { return false; }
}

// "a,b,c" -> {"a","b","c"}. Empty pieces are dropped, so a trailing comma is not an
// error — it is the shape a hand-edited list acquires.
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (const char c : s) {
        if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool fail(std::string* why, const std::string& msg) {
    if (why) *why = msg;
    return false;
}

Effect effect_from(const std::string& s) {
    if (s == "burn")     return Effect::Burn;
    if (s == "paralyze") return Effect::Paralyze;
    if (s == "sleep")    return Effect::Sleep;
    return Effect::None;
}

} // namespace

// ---- lookups -------------------------------------------------------------------

int TypeChart::index(const std::string& name) const {
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i] == name) return static_cast<int>(i);
    return -1;
}

int TypeChart::multiplier(int attacker, int defender) const {
    const int n = static_cast<int>(names.size());
    if (attacker < 0 || attacker >= n || defender < 0 || defender >= n) return kNeutral;
    return eff[static_cast<std::size_t>(attacker) * names.size() +
               static_cast<std::size_t>(defender)];
}

int Dex::move_index(const std::string& name) const {
    for (std::size_t i = 0; i < moves.size(); ++i)
        if (moves[i].name == name) return static_cast<int>(i);
    return -1;
}

const MoveDef* Dex::move(int index) const {
    if (index < 0 || index >= static_cast<int>(moves.size())) return nullptr;
    return &moves[static_cast<std::size_t>(index)];
}

const SpeciesDef* Dex::species_by_id(int id) const {
    for (const SpeciesDef& s : species)
        if (s.id == id) return &s;
    return nullptr;
}

// ---- parsing -------------------------------------------------------------------

bool parse_into(Dex& into, const std::string& text, std::string* why) {
    std::istringstream in(text);
    std::string line;
    int lineno = 0;

    while (std::getline(in, line)) {
        ++lineno;
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ln(line);
        std::string kind;
        if (!(ln >> kind)) continue;
        const std::string at = " (line " + std::to_string(lineno) + ")";

        if (kind == "type") {
            std::string name;
            if (!(ln >> name)) return fail(why, "type needs a name" + at);
            if (into.types.index(name) >= 0)
                return fail(why, "type '" + name + "' declared twice" + at);
            // Growing the table means re-laying it out: it is stored row-major, so a
            // new type is a new column inside every existing row, not an append.
            const std::size_t n = into.types.names.size() + 1;
            std::vector<int> grown(n * n, kNeutral);
            for (std::size_t a = 0; a + 1 < n; ++a)
                for (std::size_t d = 0; d + 1 < n; ++d)
                    grown[a * n + d] = into.types.eff[a * (n - 1) + d];
            into.types.names.push_back(name);
            into.types.eff = std::move(grown);

        } else if (kind == "eff") {
            std::string a, d, pct;
            if (!(ln >> a >> d >> pct)) return fail(why, "eff needs <atk> <def> <pct>" + at);
            const int ai = into.types.index(a), di = into.types.index(d);
            if (ai < 0) return fail(why, "eff names unknown type '" + a + "'" + at);
            if (di < 0) return fail(why, "eff names unknown type '" + d + "'" + at);
            int v = 0;
            if (!to_int(pct, v) || v < 0 || v > 400)
                return fail(why, "eff percent '" + pct + "' out of 0..400" + at);
            into.types.eff[static_cast<std::size_t>(ai) * into.types.names.size() +
                           static_cast<std::size_t>(di)] = v;

        } else if (kind == "move") {
            MoveDef m;
            if (!(ln >> m.name)) return fail(why, "move needs a name" + at);
            if (into.move_index(m.name) >= 0)
                return fail(why, "move '" + m.name + "' declared twice" + at);
            std::string tok, k, v;
            while (ln >> tok) {
                if (!split_kv(tok, k, v)) return fail(why, "move: '" + tok + "' is not key=value" + at);
                int n = 0;
                if (k == "type") {
                    m.type = into.types.index(v);
                    if (m.type < 0) return fail(why, "move '" + m.name + "' has unknown type '" + v + "'" + at);
                } else if (k == "power" && to_int(v, n)) m.power = n;
                else if (k == "acc"   && to_int(v, n)) m.acc = n;
                else if (k == "pp"    && to_int(v, n)) m.pp = n;
                else if (k == "pri"   && to_int(v, n)) m.priority = n;
                else if (k == "effect") {
                    const auto parts = split(v, ':');
                    if (parts.size() != 2) return fail(why, "effect wants name:percent" + at);
                    m.effect = effect_from(parts[0]);
                    if (m.effect == Effect::None)
                        return fail(why, "unknown effect '" + parts[0] + "'" + at);
                    if (!to_int(parts[1], m.effect_chance) ||
                        m.effect_chance < 1 || m.effect_chance > 100)
                        return fail(why, "effect chance out of 1..100" + at);
                } else if (k == "power" || k == "acc" || k == "pp" || k == "pri") {
                    return fail(why, "move '" + m.name + "': " + k + "='" + v + "' is not a number" + at);
                }
                // any other key: ignored on purpose, so a later field is additive
            }
            if (m.power < 0)  return fail(why, "move '" + m.name + "' has negative power" + at);
            if (m.acc < 1 || m.acc > 100)
                return fail(why, "move '" + m.name + "' accuracy out of 1..100" + at);
            if (m.pp < 1)     return fail(why, "move '" + m.name + "' has no PP" + at);
            into.moves.push_back(m);

        } else if (kind == "species") {
            SpeciesDef s;
            std::string id;
            if (!(ln >> id >> s.name)) return fail(why, "species needs <id> <name>" + at);
            if (!to_int(id, s.id) || s.id < 1)
                return fail(why, "species id '" + id + "' is not a positive number" + at);
            if (into.species_by_id(s.id))
                return fail(why, "species id " + id + " declared twice" + at);
            std::string tok, k, v;
            while (ln >> tok) {
                if (!split_kv(tok, k, v)) return fail(why, "species: '" + tok + "' is not key=value" + at);
                int n = 0;
                if (k == "type") {
                    s.type = into.types.index(v);
                    if (s.type < 0) return fail(why, "species '" + s.name + "' has unknown type '" + v + "'" + at);
                } else if (k == "hp"    && to_int(v, n)) s.hp = n;
                else if (k == "atk"     && to_int(v, n)) s.atk = n;
                else if (k == "def"     && to_int(v, n)) s.def = n;
                else if (k == "spd"     && to_int(v, n)) s.spd = n;
                else if (k == "catch"   && to_int(v, n)) s.catch_rate = n;
                else if (k == "sprite") s.sprite = v;
                else if (k == "evolve") {
                    const auto parts = split(v, '@');
                    if (parts.size() != 2 || !to_int(parts[0], s.evolve_to) ||
                        !to_int(parts[1], s.evolve_at) || s.evolve_to < 1 || s.evolve_at < 2)
                        return fail(why, "evolve wants <species>@<level>" + at);
                } else if (k == "moves") {
                    for (const std::string& e : split(v, ',')) {
                        const auto lm = split(e, ':');
                        int lvl = 0;
                        if (lm.size() != 2 || !to_int(lm[0], lvl) || lvl < 1)
                            return fail(why, "moves wants <level>:<move>, got '" + e + "'" + at);
                        s.learnset.emplace_back(lvl, lm[1]);
                    }
                } else if (k == "hp" || k == "atk" || k == "def" || k == "spd" || k == "catch") {
                    return fail(why, "species '" + s.name + "': " + k + "='" + v + "' is not a number" + at);
                }
            }
            if (s.hp < 1 || s.atk < 1 || s.def < 1 || s.spd < 1)
                return fail(why, "species '" + s.name + "' has a stat below 1" + at);
            if (s.catch_rate < 1 || s.catch_rate > 255)
                return fail(why, "species '" + s.name + "' catch rate out of 1..255" + at);
            into.species.push_back(s);

        } else {
            return fail(why, "unknown record '" + kind + "'" + at);
        }
    }
    return true;
}

std::vector<std::string> validate(const Dex& d) {
    std::vector<std::string> bad;
    if (d.types.names.empty()) bad.push_back("no types declared");
    if (d.moves.empty())       bad.push_back("no moves declared");
    if (d.species.empty())     bad.push_back("no species declared");

    for (const SpeciesDef& s : d.species) {
        if (s.learnset.empty()) {
            bad.push_back("species '" + s.name + "' knows no moves");
        } else if (s.learnset.front().first != 1) {
            // A creature is created at whatever level a route says. If nothing is
            // learned at 1 there is a level band where it has an empty move list and
            // literally cannot act, and the battle would deadlock rather than crash.
            bad.push_back("species '" + s.name + "' learns nothing at level 1");
        }
        for (const auto& [lvl, name] : s.learnset)
            if (d.move_index(name) < 0)
                bad.push_back("species '" + s.name + "' learns unknown move '" + name + "'");
        if (s.evolve_to != 0) {
            if (!d.species_by_id(s.evolve_to))
                bad.push_back("species '" + s.name + "' evolves into unknown id " +
                              std::to_string(s.evolve_to));
            else if (s.evolve_to == s.id)
                bad.push_back("species '" + s.name + "' evolves into itself");
        }
        if (s.sprite.empty())
            bad.push_back("species '" + s.name + "' has no sprite");
    }
    return bad;
}

} // namespace creature
