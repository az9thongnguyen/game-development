// =============================================================================
//  games/farm/defs.hpp  —  what a crop is, and what an item is, as DATA
// =============================================================================
//  The crops and items live in text under `assets/farm/`, not in C++ literals. That
//  is the difference between a game someone can author and a game someone has to
//  recompile: the balance pass (how long a parsnip takes, what it sells for) is the
//  work, and it should not need a build.
//
//      crop parsnip season=spring days=4 stages=5 sell=35 seed=20
//      item hoe    type=tool  tier=1
//      item parsnip type=crop sell=35
//
//  Unknown keys are ignored so a later field is additive; a malformed NUMBER is an
//  error, because silently reading `days=four` as 0 makes a crop that never grows and
//  no message saying why.
//
//  PURE: text in, structs out.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace farm {

struct CropDef {
    std::string name;
    std::string season = "spring";
    int days   = 4;        // days from planting to harvest
    int stages = 5;        // sprite stages, including seed and ripe
    int sell   = 35;       // gold per harvested crop
    int seed   = 20;       // gold per seed
};

struct ItemDef {
    std::string name;
    std::string type = "crop";   // tool | seed | crop
    int         sell = 0;
    int         tier = 0;
};

struct Defs {
    std::vector<CropDef> crops;
    std::vector<ItemDef> items;

    [[nodiscard]] int            crop_index(const std::string& name) const;
    [[nodiscard]] const CropDef* crop(const std::string& name) const;
    [[nodiscard]] const ItemDef* item(const std::string& name) const;
};

// Parse one definitions file. Both `crop` and `item` lines may appear in either file,
// so a small game can keep everything in one and a larger one can split them.
std::optional<Defs> parse_defs(const std::string& text);

// Merge `more` into `into` (later definitions of the same name REPLACE earlier ones,
// so an override file can sit after the base one).
void merge_defs(Defs& into, const Defs& more);

// ---- overrides: the same text, from somewhere nobody can rebuild ----------------
//
// Remote config and live events deliver balance changes in THIS format — the operator
// edits the same lines they would edit in the file. What they do not get is
// `merge_defs`: that replaces a whole record, so `crop parsnip sell=70` typed into a
// dashboard would silently reset days/stages/seed to the struct defaults, and a
// price change would quietly re-balance growth. An override assigns only the fields
// the line actually names.
//
// The other difference is what happens to a mistake. A FILE is additive and
// forward-compatible, so parse_defs ignores an unknown key. A dashboard field was
// typed by a person thirty seconds ago, so an unknown key there is a typo and is
// reported. Nothing is applied from a line that fails, and a line that would make a
// crop unplayable (days < 1, stages < 2) is refused with the rest of them: remote
// config must not be able to brick a running game.
struct OverrideReport {
    int                      applied = 0;   // fields actually assigned
    std::vector<std::string> problems;      // one line each, naming the record
};
OverrideReport apply_overrides(Defs& into, const std::string& text);

} // namespace farm
