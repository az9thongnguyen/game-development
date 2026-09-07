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

namespace {

// One `key=value` onto a crop. Returns an error phrase, or "" when it assigned.
//
// `unknown_is_error` is the ONE difference between this function's two callers, and it
// is a real difference: a `.def` FILE must stay forward-compatible, so a key it has
// never heard of is ignored and a newer file still loads in an older build. An
// OVERRIDE arriving from remote config must not silently no-op — an operator who types
// `sel=40` has to be told, or the price they think they changed is the price they did
// not.
//
// Everything else — which keys exist and which values are legal — lives here once. It
// did not until chapter 143: `parse_defs` carried its own copy of this dispatch, and
// when `season` grew a validity rule the copy never got it, so a file could define a
// crop whose season did not exist and the game refused to plant it with no explanation.
std::string assign_crop(CropDef& c, const std::string& k, const std::string& v,
                        bool unknown_is_error) {
    if (k == "season") {
        if (!valid_season_word(v)) return "'" + v + "' is not a season";
        c.season = v;
        return {};
    }
    if (k == "days")   return to_int(v, c.days)   ? std::string() : "'" + v + "' is not a number";
    if (k == "stages") return to_int(v, c.stages) ? std::string() : "'" + v + "' is not a number";
    if (k == "sell")   return to_int(v, c.sell)   ? std::string() : "'" + v + "' is not a number";
    if (k == "seed")   return to_int(v, c.seed)   ? std::string() : "'" + v + "' is not a number";
    return unknown_is_error ? "unknown field '" + k + "'" : std::string();
}

std::string assign_item(ItemDef& i, const std::string& k, const std::string& v,
                        bool unknown_is_error) {
    if (k == "type") { i.type = v; return {}; }
    if (k == "sell") return to_int(v, i.sell) ? std::string() : "'" + v + "' is not a number";
    if (k == "tier") return to_int(v, i.tier) ? std::string() : "'" + v + "' is not a number";
    return unknown_is_error ? "unknown field '" + k + "'" : std::string();
}

// A crop with no stages divides by zero when its growth is drawn; a crop with no days
// is ripe the instant it is planted. Checked on both roads in, because remote config
// must not be able to brick a running game either.
bool playable(const CropDef& c) { return c.days >= 1 && c.stages >= 2; }

}  // namespace

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
                // false: an unknown key is IGNORED here, so a later field is purely
                // additive and an older build still reads a newer file.
                if (!assign_crop(c, k, v, /*unknown_is_error=*/false).empty())
                    return std::nullopt;
            }
            if (!playable(c)) return std::nullopt;
            d.crops.push_back(std::move(c));
        } else if (kind == "item") {
            ItemDef it;
            if (!(ln >> it.name)) return std::nullopt;
            std::string tok, k, v;
            while (ln >> tok) {
                if (!split_kv(tok, k, v)) return std::nullopt;
                if (!assign_item(it, k, v, /*unknown_is_error=*/false).empty())
                    return std::nullopt;
            }
            d.items.push_back(std::move(it));
        }
        // An unknown record kind is skipped rather than fatal: the same file may later
        // carry `shop` or `recipe` lines a different module reads.
    }
    return d;
}

// ---- the calendar ----------------------------------------------------------------

Season season_of(int day) {
    // `day` is 1-based and could be anything a save file says. Floor-divide so day 0
    // and a negative day still land somewhere real rather than reading off the end.
    const int index = (day - 1) % (kSeasonsPerYear * kDaysPerSeason);
    const int wrapped = index < 0 ? index + kSeasonsPerYear * kDaysPerSeason : index;
    return static_cast<Season>(wrapped / kDaysPerSeason);
}

int day_of_season(int day) {
    const int index = (day - 1) % kDaysPerSeason;
    return (index < 0 ? index + kDaysPerSeason : index) + 1;
}

const char* season_name(Season s) {
    switch (s) {
        case Season::Spring: return "spring";
        case Season::Summer: return "summer";
        case Season::Autumn: return "autumn";
        case Season::Winter: return "winter";
    }
    return "spring";
}

std::optional<Season> season_from_string(const std::string& s) {
    if (s == "spring") return Season::Spring;
    if (s == "summer") return Season::Summer;
    if (s == "autumn") return Season::Autumn;
    if (s == "winter") return Season::Winter;
    return std::nullopt;   // including "all" — that is not A season, it is all of them
}

namespace {
// The one place the year-round spelling lives. `valid_season_word` and `grows_in` are
// two different questions about the same word, and this is what stops them answering
// differently — which is how `season=any` becomes loadable and unplantable.
bool is_every_season(const std::string& s) { return s == "all" || s == "any"; }
}  // namespace

bool valid_season_word(const std::string& s) {
    return is_every_season(s) || season_from_string(s).has_value();
}

bool grows_in(const CropDef& c, Season s) {
    if (is_every_season(c.season)) return true;
    const auto want = season_from_string(c.season);
    return want && *want == s;
}

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
            why = kind == "crop" ? assign_crop(c, k, v, /*unknown_is_error=*/true)
                                 : assign_item(i, k, v, /*unknown_is_error=*/true);
            if (why.empty()) ++applied;
        }
        // Remote config must not be able to brick a running game: a crop that never
        // grows, or one whose stage count divides by zero when it is drawn, is refused
        // here rather than discovered on the field.
        if (why.empty() && kind == "crop" && !playable(c))
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
