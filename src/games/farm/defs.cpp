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
