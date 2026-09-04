// =============================================================================
//  games/farm/defs.cpp
// =============================================================================
#include "games/farm/defs.hpp"

#include <sstream>

namespace farm {

namespace {

// "key=value" -> both halves. Returns false for a token with no '=' (the caller then
// knows it is positional, like the name).
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
        if (used != s.size()) return false;      // "4x" is a typo, not the number 4
        out = v;
        return true;
    } catch (...) { return false; }
}

} // namespace

int Defs::crop_index(const std::string& name) const {
    for (std::size_t i = 0; i < crops.size(); ++i)
        if (crops[i].name == name) return static_cast<int>(i);
    return -1;
}

const CropDef* Defs::crop(const std::string& name) const {
    const int i = crop_index(name);
    return i < 0 ? nullptr : &crops[static_cast<std::size_t>(i)];
}

const ItemDef* Defs::item(const std::string& name) const {
    for (const ItemDef& it : items)
        if (it.name == name) return &it;
    return nullptr;
}

std::optional<Defs> parse_defs(const std::string& text) {
    Defs d;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ln(line);
        std::string kind;
        if (!(ln >> kind)) continue;

        if (kind == "crop") {
            CropDef c;
            if (!(ln >> c.name)) return std::nullopt;
            std::string tok, k, v;
            while (ln >> tok) {
                if (!split_kv(tok, k, v)) return std::nullopt;
                if      (k == "season") c.season = v;
                else if (k == "days")   { if (!to_int(v, c.days))   return std::nullopt; }
                else if (k == "stages") { if (!to_int(v, c.stages)) return std::nullopt; }
                else if (k == "sell")   { if (!to_int(v, c.sell))   return std::nullopt; }
                else if (k == "seed")   { if (!to_int(v, c.seed))   return std::nullopt; }
                // anything else: ignored, so a later field is purely additive
            }
            // A crop with no stages would divide by zero when growth is drawn; a crop
            // with no days would be ripe the instant it is planted.
            if (c.days < 1 || c.stages < 2) return std::nullopt;
            d.crops.push_back(std::move(c));
        } else if (kind == "item") {
            ItemDef it;
            if (!(ln >> it.name)) return std::nullopt;
            std::string tok, k, v;
            while (ln >> tok) {
                if (!split_kv(tok, k, v)) return std::nullopt;
                if      (k == "type") it.type = v;
                else if (k == "sell") { if (!to_int(v, it.sell)) return std::nullopt; }
                else if (k == "tier") { if (!to_int(v, it.tier)) return std::nullopt; }
            }
            d.items.push_back(std::move(it));
        }
        // An unknown record kind is skipped rather than fatal: the same file may later
        // carry `shop` or `recipe` lines a different module reads.
    }
    return d;
}

namespace {

// One `key=value` onto a crop. Returns an error phrase, or "" when it assigned.
// Split out so the two record kinds share the "what went wrong" wording, and so the
// caller can decide to apply nothing when any field fails.
std::string assign_crop(CropDef& c, const std::string& k, const std::string& v) {
    if (k == "season") { c.season = v; return {}; }
    if (k == "days")   return to_int(v, c.days)   ? std::string() : "'" + v + "' is not a number";
    if (k == "stages") return to_int(v, c.stages) ? std::string() : "'" + v + "' is not a number";
    if (k == "sell")   return to_int(v, c.sell)   ? std::string() : "'" + v + "' is not a number";
    if (k == "seed")   return to_int(v, c.seed)   ? std::string() : "'" + v + "' is not a number";
    return "unknown field '" + k + "'";
}

std::string assign_item(ItemDef& i, const std::string& k, const std::string& v) {
    if (k == "type") { i.type = v; return {}; }
    if (k == "sell") return to_int(v, i.sell) ? std::string() : "'" + v + "' is not a number";
    if (k == "tier") return to_int(v, i.tier) ? std::string() : "'" + v + "' is not a number";
    return "unknown field '" + k + "'";
}

} // namespace

OverrideReport apply_overrides(Defs& into, const std::string& text) {
    OverrideReport rep;
    std::istringstream in(text);
    std::string        line;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ln(line);
        std::string        kind, name;
        if (!(ln >> kind)) continue;
        if (kind != "crop" && kind != "item") {
            rep.problems.push_back("unknown record '" + kind + "'");
            continue;
        }
        if (!(ln >> name)) { rep.problems.push_back(kind + " line with no name"); continue; }
        const std::string where = kind + " " + name;

        // Locate the record first: an override names something that already exists, so
        // an unknown name is a typo, not a new definition. Adding a crop from a
        // dashboard would give the operator a way to ship content that no client has
        // a sprite, a season or a seed item for.
        const int at   = kind == "crop" ? into.crop_index(name) : -1;
        ItemDef*  item = nullptr;
        if (kind == "item")
            for (ItemDef& it : into.items)
                if (it.name == name) { item = &it; break; }
        if ((kind == "crop" && at < 0) || (kind == "item" && item == nullptr)) {
            rep.problems.push_back("unknown " + where);
            continue;
        }

        // Everything lands on a COPY and is committed only if the whole line worked. A
        // line that sets three fields and mistypes the fourth must not leave the record
        // half-changed: that is a balance nobody chose and nobody can see.
        CropDef c = at >= 0 ? into.crops[static_cast<std::size_t>(at)] : CropDef{};
        ItemDef i = item ? *item : ItemDef{};

        int         applied = 0;
        std::string tok, k, v, why;
        while (why.empty() && (ln >> tok)) {
            if (!split_kv(tok, k, v)) { why = "'" + tok + "' is not key=value"; break; }
            why = kind == "crop" ? assign_crop(c, k, v) : assign_item(i, k, v);
            if (why.empty()) ++applied;
        }
        // Remote config must not be able to brick a running game: a crop that never
        // grows, or one whose stage count divides by zero when it is drawn, is refused
        // here rather than discovered on the field.
        if (why.empty() && kind == "crop" && (c.days < 1 || c.stages < 2))
            why = "days/stages would make it unplayable";

        if (!why.empty()) { rep.problems.push_back(where + ": " + why); continue; }
        if (at >= 0) into.crops[static_cast<std::size_t>(at)] = c;
        else         *item = i;
        rep.applied += applied;
    }
    return rep;
}

void merge_defs(Defs& into, const Defs& more) {
    for (const CropDef& c : more.crops) {
        const int at = into.crop_index(c.name);
        if (at >= 0) into.crops[static_cast<std::size_t>(at)] = c;
        else         into.crops.push_back(c);
    }
    for (const ItemDef& i : more.items) {
        bool replaced = false;
        for (ItemDef& existing : into.items)
            if (existing.name == i.name) { existing = i; replaced = true; break; }
        if (!replaced) into.items.push_back(i);
    }
}

} // namespace farm
