// =============================================================================
//  games/farm/world.cpp
// =============================================================================
#include "games/farm/world.hpp"

#include <algorithm>
#include <sstream>

namespace farm {

namespace {

constexpr int kEnergyHoe     = 2;
constexpr int kEnergyWater   = 2;
constexpr int kEnergyPlant   = 1;
constexpr int kEnergyHarvest = 1;
// A collapse costs the morning: you wake with less than a full night's rest.
constexpr int kCollapseEnergy = kMaxEnergy / 2;

std::string two(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }

void hash_mix(std::uint64_t& h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
}

} // namespace

// ---- Rng ---------------------------------------------------------------------

std::uint64_t Rng::next() {
    s_ ^= s_ >> 12;
    s_ ^= s_ << 25;
    s_ ^= s_ >> 27;
    return s_ * 0x2545F4914F6CDD1Dull;
}

int Rng::range(int lo, int hi) {
    if (lo >= hi) return lo;
    const std::uint64_t span = static_cast<std::uint64_t>(hi - lo) + 1;
    return lo + static_cast<int>(next() % span);
}

// ---- keys and queries ----------------------------------------------------------

long long soil_key(int x, int y) {
    // 32 bits each, biased so negatives pack without sign trouble.
    return (static_cast<long long>(static_cast<std::uint32_t>(x)) << 32) |
           static_cast<std::uint32_t>(y);
}

void soil_unkey(long long k, int& x, int& y) {
    x = static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(k) >> 32));
    y = static_cast<int>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(k) & 0xFFFFFFFFull));
}

std::string seed_item(const std::string& crop) { return crop + "_seed"; }

std::string World::time_text() const {
    return two(hour()) + ":" + two(min_of_hour());
}

const Soil* World::at(int x, int y) const {
    const auto it = soil.find(soil_key(x, y));
    return it == soil.end() ? nullptr : &it->second;
}

// ---- clock ---------------------------------------------------------------------

bool advance(World& w, double dt_seconds) {
    if (w.minute >= kCollapseMin) return false;      // already past; report once only
    static_assert(kSecondsPerGameMinute > 0.0, "a day with no length never ends");
    // The CLOCK is an integer; only the remainder is floating point. A frame is far
    // shorter than a game minute, so truncating per call would stop time altogether —
    // the accumulator is what makes the day take the same wall-clock length at 30 fps
    // and at 144, while every simulation decision still lands on a whole minute.
    if (dt_seconds > 0.0) w.clock_accum += dt_seconds;
    const int whole = static_cast<int>(w.clock_accum / kSecondsPerGameMinute);
    if (whole <= 0) return false;
    w.clock_accum -= whole * kSecondsPerGameMinute;
    w.minute += whole;
    if (w.minute >= kCollapseMin) {
        w.minute = kCollapseMin;
        return true;
    }
    return false;
}

// ---- actions -------------------------------------------------------------------

ActionResult use_tool(World& w, const Defs& defs, const tilemap::Map& map, Tool tool,
                      int x, int y, const std::string& seed_item_id) {
    const auto fail = [](std::string m) { return ActionResult{false, std::move(m), 0}; };
    if (!map.in_bounds(x, y)) return fail("that is not part of the farm");
    if (map.solid(x, y))      return fail("something is in the way");
    if (w.energy <= 0)        return fail("too tired - go to bed");

    const long long key = soil_key(x, y);
    Soil& s = w.soil[key];              // default-constructs untouched ground

    const auto spend = [&w](int cost, std::string msg) {
        w.energy = std::max(0, w.energy - cost);
        return ActionResult{true, std::move(msg), cost};
    };

    switch (tool) {
        case Tool::Hoe:
            if (s.crop >= 0) return fail("something is growing there");
            if (s.tilled)    return fail("already tilled");
            s.tilled = true;
            return spend(kEnergyHoe, "tilled the soil");

        case Tool::Water:
            if (!s.tilled) { w.soil.erase(key); return fail("nothing to water"); }
            if (s.watered) return fail("already watered");
            s.watered = true;
            return spend(kEnergyWater, "watered");

        case Tool::Seed: {
            if (!s.tilled) { w.soil.erase(key); return fail("till the soil first"); }
            if (s.crop >= 0) return fail("something is already planted");
            const int idx = defs.crop_index(seed_item_id);
            if (idx < 0) return fail("no such seed: " + seed_item_id);
            const std::string seed_name = seed_item(seed_item_id);
            auto have = w.inventory.find(seed_name);
            if (have == w.inventory.end() || have->second <= 0)
                return fail("out of " + seed_name);
            have->second -= 1;
            if (have->second == 0) w.inventory.erase(have);
            s.crop = idx;
            s.stage = 0;
            s.days_grown = 0;
            return spend(kEnergyPlant, "planted " + seed_item_id);
        }

        case Tool::Harvest: {
            if (s.crop < 0) { if (!s.tilled) w.soil.erase(key); return fail("nothing to harvest"); }
            const CropDef& c = defs.crops[static_cast<std::size_t>(s.crop)];
            if (s.stage < c.stages - 1) return fail(c.name + " is not ready");
            w.inventory[c.name] += 1;
            // Harvesting leaves TILLED soil, not raw ground: replanting should not
            // cost a second hoe swing, which is the whole rhythm of a farming day.
            s.crop = -1;
            s.stage = 0;
            s.days_grown = 0;
            return spend(kEnergyHarvest, "harvested " + c.name);
        }
    }
    return fail("unknown tool");
}

bool try_move(World& w, const tilemap::Map& map, int dx, int dy) {
    const int nx = w.px + dx, ny = w.py + dy;
    if (!map.in_bounds(nx, ny) || map.solid(nx, ny)) return false;
    w.px = nx;
    w.py = ny;
    return true;
}

// ---- the day boundary ----------------------------------------------------------

DayReport end_day(World& w, const Defs& defs, bool collapsed) {
    DayReport r;
    r.collapsed = collapsed;

    // Every random decision for this day comes from one stream seeded by (seed, day),
    // so the same save always produces the same tomorrow no matter what the player
    // did with the frame rate.
    Rng rng(w.seed ^ (static_cast<std::uint64_t>(w.day) * 0x100000001B3ull));

    for (auto& [name, count] : w.shipped) {
        if (count <= 0) continue;
        // A crop's price belongs to the CROP. It used to be whichever of the two files
        // mentioned the name first — and both did, with the same number, which is how
        // a price ends up written twice and changed once. Now `crop parsnip sell=40`
        // is the only line that moves a parsnip's price, wherever it arrives from:
        // the file, remote config, or a live event.
        int unit = 0;
        if (const CropDef* c = defs.crop(name)) unit = c->sell;
        else if (const ItemDef* it = defs.item(name)) unit = it->sell;
        // A small daily swing so the economy is not a spreadsheet. Deterministic,
        // because it comes out of the day's own stream.
        const int jitter = rng.range(-2, 2);
        r.gold_earned += std::max(0, (unit + jitter)) * count;
    }
    w.gold += r.gold_earned;
    w.shipped.clear();

    for (auto& [key, s] : w.soil) {
        if (s.crop < 0) { s.watered = false; continue; }
        const CropDef& c = defs.crops[static_cast<std::size_t>(s.crop)];
        if (s.watered && s.stage < c.stages - 1) {
            s.days_grown += 1;
            // RIPENESS and APPEARANCE are decided separately, on purpose. Deriving
            // "ripe" from a rounded stage index made a crop harvestable early whenever
            // the stage count and the day count did not divide evenly: the last stage
            // is the harvestable one, and rounding up reached it a day too soon.
            if (s.days_grown >= c.days) {
                s.stage = c.stages - 1;
                ++r.crops_grown;
            } else {
                // Intermediate stages spread over the growing days, and never reach
                // the last one — that index means ripe and nothing else.
                s.stage = std::min(c.stages - 2, s.days_grown * (c.stages - 1) / c.days);
            }
        }
        s.watered = false;      // yesterday's water does not count for today
    }

    w.day += 1;
    w.minute = kDayStartMin;
    w.clock_accum = 0.0;
    w.energy = collapsed ? kCollapseEnergy : kMaxEnergy;
    r.day = w.day;
    return r;
}

// ---- schedules -----------------------------------------------------------------

std::string Schedule::place_at(int minute) const {
    if (entries.empty()) return {};
    std::string place = entries.back().place;   // before the first entry: where they slept
    for (const ScheduleEntry& e : entries) {
        if (e.minute > minute) break;
        place = e.place;
    }
    return place;
}

std::optional<Schedule> parse_schedule(const std::string& text) {
    Schedule s;
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) return std::nullopt;
    {
        std::istringstream head(line);
        std::string magic; int version = 0;
        if (!(head >> magic >> version) || magic != "sched") return std::nullopt;
        if (version > 1) return std::nullopt;
    }
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ln(line);
        std::string tok;
        if (!(ln >> tok)) continue;
        if (tok == "npc") {
            ln >> s.npc;
        } else if (tok == "at") {
            std::string when, place;
            if (!(ln >> when >> place)) return std::nullopt;
            const auto colon = when.find(':');
            if (colon == std::string::npos) return std::nullopt;
            try {
                const int h = std::stoi(when.substr(0, colon));
                const int m = std::stoi(when.substr(colon + 1));
                if (h < 0 || h > 47 || m < 0 || m > 59) return std::nullopt;
                s.entries.push_back(ScheduleEntry{h * 60 + m, place});
            } catch (...) { return std::nullopt; }
        }
    }
    // Sorting here rather than requiring it in the file: a schedule written out of
    // order is a plausible thing for a person to do, and place_at depends on order.
    std::stable_sort(s.entries.begin(), s.entries.end(),
                     [](const ScheduleEntry& a, const ScheduleEntry& b) {
                         return a.minute < b.minute;
                     });
    return s;
}

void update_npcs(World& w, const tilemap::Map& map, const std::vector<Schedule>& schedules) {
    for (NpcState& n : w.npcs) {
        for (const Schedule& s : schedules) {
            if (s.npc != n.name) continue;
            const std::string place = s.place_at(w.minute);
            if (place.empty()) break;
            if (const tilemap::Entity* e = map.entity(place)) {
                n.place = place;
                n.x = e->x;
                n.y = e->y;
            }
            break;
        }
    }
}

// ---- persistence ---------------------------------------------------------------

doc::SaveState to_save(const World& w) {
    doc::SaveState s;
    s.game    = "farm";
    s.version = kSaveVersion;
    s.set("seed",   static_cast<long long>(w.seed));
    s.set("day",    w.day);
    s.set("minute", w.minute);
    s.set("energy", w.energy);
    s.set("gold",   w.gold);
    s.set("px",     w.px);
    s.set("py",     w.py);

    std::string soil;
    for (const auto& [key, t] : w.soil) {
        int x = 0, y = 0;
        soil_unkey(key, x, y);
        soil += std::to_string(x) + " " + std::to_string(y) + " " + (t.tilled ? "1" : "0") +
                " " + (t.watered ? "1" : "0") + " " + std::to_string(t.crop) + " " +
                std::to_string(t.stage) + " " + std::to_string(t.days_grown) + "\n";
    }
    s.sections["soil"] = soil;

    std::string inv;
    for (const auto& [name, n] : w.inventory) inv += name + " " + std::to_string(n) + "\n";
    s.sections["inventory"] = inv;

    std::string ship;
    for (const auto& [name, n] : w.shipped) ship += name + " " + std::to_string(n) + "\n";
    s.sections["shipped"] = ship;
    return s;
}

std::optional<World> from_save(const doc::SaveState& s) {
    if (s.game != "farm") return std::nullopt;
    World w;
    w.seed   = static_cast<std::uint64_t>(s.num("seed", 1));
    w.day    = static_cast<int>(s.num("day", 1));
    w.minute = static_cast<int>(s.num("minute", kDayStartMin));
    w.energy = static_cast<int>(s.num("energy", kMaxEnergy));
    w.gold   = static_cast<int>(s.num("gold", 0));
    w.px     = static_cast<int>(s.num("px", 0));
    w.py     = static_cast<int>(s.num("py", 0));

    if (const auto it = s.sections.find("soil"); it != s.sections.end()) {
        std::istringstream in(it->second);
        int x, y, tilled, watered, crop, stage, grown;
        while (in >> x >> y >> tilled >> watered >> crop >> stage >> grown) {
            Soil t;
            t.tilled = tilled != 0;
            t.watered = watered != 0;
            t.crop = crop;
            t.stage = stage;
            t.days_grown = grown;
            w.soil[soil_key(x, y)] = t;
        }
    }
    const auto read_counts = [&s](const char* name, std::map<std::string, int>& into) {
        const auto it = s.sections.find(name);
        if (it == s.sections.end()) return;
        std::istringstream in(it->second);
        std::string item; int n = 0;
        while (in >> item >> n) into[item] = n;
    };
    read_counts("inventory", w.inventory);
    read_counts("shipped", w.shipped);
    return w;
}

std::vector<doc::Migration> migrations() {
    // Version 1 is the first shipped format, so the chain is empty. It exists as a
    // named, tested seam so the first migration is an ADDITION rather than a design.
    return {};
}

std::uint64_t hash(const World& w) {
    std::uint64_t h = 1469598103934665603ull;
    hash_mix(h, w.seed);
    hash_mix(h, static_cast<std::uint64_t>(w.day));
    hash_mix(h, static_cast<std::uint64_t>(w.minute));
    hash_mix(h, static_cast<std::uint64_t>(w.energy));
    hash_mix(h, static_cast<std::uint64_t>(w.gold));
    hash_mix(h, static_cast<std::uint64_t>(w.px));
    hash_mix(h, static_cast<std::uint64_t>(w.py));
    for (const auto& [key, s] : w.soil) {
        hash_mix(h, static_cast<std::uint64_t>(key));
        hash_mix(h, static_cast<std::uint64_t>((s.tilled ? 1 : 0) | (s.watered ? 2 : 0)));
        hash_mix(h, static_cast<std::uint64_t>(s.crop + 1));
        hash_mix(h, static_cast<std::uint64_t>(s.stage));
        hash_mix(h, static_cast<std::uint64_t>(s.days_grown));
    }
    for (const auto& [name, n] : w.inventory) {
        for (char c : name) hash_mix(h, static_cast<std::uint64_t>(c));
        hash_mix(h, static_cast<std::uint64_t>(n));
    }
    for (const auto& [name, n] : w.shipped) {
        for (char c : name) hash_mix(h, static_cast<std::uint64_t>(c));
        hash_mix(h, static_cast<std::uint64_t>(n));
    }
    return h;
}

} // namespace farm
