// =============================================================================
//  games/farm/world.hpp  —  the farm simulation (deterministic, no renderer)
// =============================================================================
//  Everything the game IS, with nothing about how it looks. A scene reads this and
//  draws it; a test runs three simulated days and checks the crops came up on
//  schedule. The split is what lets the day loop be verified without a window.
//
//  Determinism is a design constraint, not a nicety: `Rng` is seeded from
//  (save_seed, day), so replaying the same actions on the same save produces the same
//  world. That is what makes a save file meaningful, and it is the same property the
//  Creature RPG's battle replay will need later.
//
//  Time: a day runs 06:00 -> 02:00 (20 in-game hours). Past 02:00 the player
//  collapses, which is the game's way of saying "the clock is a resource".
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/document/save.hpp"
#include "engine/tilemap/map2.hpp"
#include "games/farm/defs.hpp"

namespace farm {

inline constexpr int kSaveVersion  = 1;
inline constexpr int kDayStartMin  = 6 * 60;    // 06:00
inline constexpr int kCollapseMin  = 26 * 60;   // 02:00 the following morning
inline constexpr int kMaxEnergy    = 100;
inline constexpr double kSecondsPerGameMinute = 0.6;   // 12 real minutes per day

// xorshift64*. Small, deterministic, and identical on every platform — std::mt19937
// is portable but std::uniform_int_distribution is NOT, and a save that replays
// differently on the web build than on the desktop is not a save.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : s_(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    std::uint64_t next();
    int range(int lo, int hi);            // inclusive; lo > hi returns lo
private:
    std::uint64_t s_;
};

enum class Tool { Hoe, Water, Seed, Harvest };

// The inventory item that plants a crop. Item ids are SINGLE TOKENS with no spaces:
// the save format stores them as `<name> <count>` per line, and a name with a space
// in it would read back as a name and a broken number.
std::string seed_item(const std::string& crop);

struct Soil {
    bool tilled  = false;
    bool watered = false;
    int  crop    = -1;      // index into Defs::crops, -1 = nothing planted
    int  stage   = 0;       // 0 .. CropDef::stages-1; the last stage is harvestable
    int  days_grown = 0;
};

// The outcome of an action, in the same shape the rest of the project uses for
// "did it work, and what do I tell the player".
struct ActionResult {
    bool        ok = false;
    std::string message;
    int         energy_spent = 0;
};

struct NpcState {
    std::string name;
    std::string place;      // the map entity the NPC is standing at
    int x = 0, y = 0;
};

struct World {
    // ---- persistent ----
    std::uint64_t seed = 1;
    int  day    = 1;
    int  minute = kDayStartMin;
    int  energy = kMaxEnergy;
    int  gold   = 0;
    int  px = 0, py = 0;                       // player tile
    // Sub-minute remainder. Kept OUT of the hash and out of the save on purpose: a
    // world is identified by the minute it is on, not by where inside that minute a
    // particular frame rate happened to leave it.
    double clock_accum = 0.0;
    std::map<long long, Soil>          soil;   // sparse: only worked tiles exist
    std::map<std::string, int>         inventory;
    std::map<std::string, int>         shipped;   // sold at the end of the day
    std::vector<NpcState>              npcs;

    // ---- queries ----
    [[nodiscard]] int  hour() const { return (minute / 60) % 24; }
    [[nodiscard]] int  min_of_hour() const { return minute % 60; }
    [[nodiscard]] std::string time_text() const;      // "06:30"
    [[nodiscard]] const Soil* at(int x, int y) const;
    [[nodiscard]] bool collapsed() const { return minute >= kCollapseMin; }
};

// Sparse-grid key. Packed rather than a pair so the map stays a plain
// std::map<long long, Soil> and the save format can print one number per tile.
long long soil_key(int x, int y);
void      soil_unkey(long long k, int& x, int& y);

// ---- simulation ----------------------------------------------------------------

// Advance the clock only. Returns true when the day rolled past 02:00 this call, so
// the caller can force the player to bed exactly once.
bool advance(World& w, double dt_seconds);

// Perform one tool action on tile (x,y). `map` supplies collision (you cannot till a
// wall) and `defs` supplies the crop being planted.
// `seed_item_id` is the CROP name for Tool::Seed (the inventory item consumed is
// seed_item(crop)); it is ignored by the other tools.
ActionResult use_tool(World& w, const Defs& defs, const tilemap::Map& map, Tool tool,
                      int x, int y, const std::string& seed_item_id = {});

// Sleep: sell the shipping box, grow what was watered, and start the next day. This
// is the only place the RNG is used, and it is re-seeded from (seed, day) so the same
// save always produces the same tomorrow.
struct DayReport {
    int gold_earned = 0;
    int crops_grown = 0;
    int day = 0;
    bool collapsed = false;    // the player did not choose to sleep; the clock did
};
DayReport end_day(World& w, const Defs& defs, bool collapsed = false);

// Move the player one tile if the destination is walkable. Returns false when blocked,
// so a caller can play a bump sound rather than guessing.
bool try_move(World& w, const tilemap::Map& map, int dx, int dy);

// ---- NPC schedules -------------------------------------------------------------

struct ScheduleEntry { int minute; std::string place; };
struct Schedule {
    std::string             npc;
    std::vector<ScheduleEntry> entries;    // sorted by minute
    // Where the NPC is at `minute`: the last entry at or before it. Before the first
    // entry, the LAST one — an NPC is somewhere overnight, not nowhere.
    [[nodiscard]] std::string place_at(int minute) const;
};
std::optional<Schedule> parse_schedule(const std::string& text);

// Put each NPC where its schedule says, resolving the place through the map's
// entities. An unknown place leaves the NPC where it was rather than at (0,0).
void update_npcs(World& w, const tilemap::Map& map,
                 const std::vector<Schedule>& schedules);

// ---- persistence ---------------------------------------------------------------

doc::SaveState            to_save(const World& w);
std::optional<World>      from_save(const doc::SaveState& s);
std::vector<doc::Migration> migrations();

// A content hash of everything that affects play. Two worlds that hash the same are
// the same run — which is how a 28-day determinism test states its claim.
std::uint64_t hash(const World& w);

} // namespace farm
