// =============================================================================
//  tests/test_farm.cpp  —  the farm simulation, three days at a time
// =============================================================================
//  The whole point of splitting the simulation from the scene is that the day loop
//  can be verified without a window. So this plays the game: till, plant, water,
//  sleep, and check the parsnip came up on the day the definition says it should.
//
//  Determinism is asserted rather than assumed: the same seed and the same actions
//  produce the same world hash, and a different seed produces a different one.
// =============================================================================
#include <set>
#include <cstdio>

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif
#include <string>
#include <vector>

#include "engine/document/save.hpp"
#include "games/farm/defs.hpp"
#include "games/farm/cloud.hpp"
#include "games/farm/controls.hpp"
#include "games/farm/theme.hpp"
#include "games/studio/recipe.hpp"
#include "games/studio/texture_gen.hpp"
#include "engine/assets.hpp"
#include "engine/image.hpp"
#include "games/farm/dialogue.hpp"
#include "games/farm/world.hpp"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

using namespace farm;

namespace {

const char* kDefs =
    "# the starter crop\n"
    "crop parsnip season=spring days=4 stages=5 sell=35 seed=20\n"
    "item parsnip type=crop sell=35\n"
    "item hoe type=tool tier=1\n";

// A 8x6 open field with a wall down column 7, so "you cannot till a wall" is testable.
tilemap::Map field() {
    tilemap::Map m;
    m.name = "field"; m.w = 8; m.h = 6; m.tile = 16;
    tilemap::Layer ground; ground.name = "ground"; ground.kind = tilemap::LayerKind::Tiles;
    ground.cells.assign(48, 1);
    tilemap::Layer collide; collide.name = "collide"; collide.kind = tilemap::LayerKind::Mask;
    collide.cells.assign(48, 0);
    for (int y = 0; y < 6; ++y) collide.cells[static_cast<std::size_t>(y) * 8 + 7] = 1;
    m.layers.push_back(std::move(ground));
    m.layers.push_back(std::move(collide));
    m.entities.push_back(tilemap::Entity{"anna_home", 1, 1, {}});
    m.entities.push_back(tilemap::Entity{"anna_shop", 5, 2, {}});
    return m;
}

Defs load_defs() {
    auto d = parse_defs(kDefs);
    return d ? *d : Defs{};
}

} // namespace

static void test_defs() {
    auto d = parse_defs(kDefs);
    CHECK(d.has_value());
    if (!d) return;
    CHECK(d->crops.size() == 1);
    CHECK(d->crop("parsnip") != nullptr);
    CHECK(d->crop("parsnip")->days == 4);
    CHECK(d->crop_index("turnip") == -1);
    CHECK(d->item("hoe") && d->item("hoe")->type == "tool");

    // A number that is not a number is an error, not a silent zero: `days=four` would
    // otherwise make a crop that never ripens with nothing to say why.
    CHECK(!parse_defs("crop bad days=four\n").has_value());
    CHECK(!parse_defs("crop bad days=4x\n").has_value());
    // ...and so are values that cannot describe a crop at all.
    CHECK(!parse_defs("crop bad days=0 stages=5\n").has_value());
    CHECK(!parse_defs("crop bad days=4 stages=1\n").has_value());
    // A bare key with no '=' is a typo, not a flag.
    CHECK(!parse_defs("crop bad days\n").has_value());
    // An unknown record kind is skipped: the same file may later carry shop lines.
    CHECK(parse_defs("shop pierre open=9\ncrop ok days=2 stages=2\n").has_value());

    // Later definitions override earlier ones, which is what an override file is for.
    Defs base = *d;
    auto over = parse_defs("crop parsnip days=2 stages=3 sell=99\n");
    CHECK(over.has_value());
    merge_defs(base, *over);
    CHECK(base.crops.size() == 1);
    CHECK(base.crop("parsnip")->sell == 99);
}

static void test_a_day_of_farming() {
    const Defs defs = load_defs();
    const tilemap::Map map = field();
    World w;
    w.seed = 42;
    w.px = 2; w.py = 2;
    w.inventory[seed_item("parsnip")] = 3;

    // You cannot till a wall, and you cannot plant on ground you have not worked.
    CHECK(!use_tool(w, defs, map, Tool::Hoe, 7, 2).ok);
    CHECK(!use_tool(w, defs, map, Tool::Seed, 2, 2, "parsnip").ok);
    CHECK(w.soil.empty());          // a failed action leaves no trace on the world

    const ActionResult hoed = use_tool(w, defs, map, Tool::Hoe, 2, 2);
    CHECK(hoed.ok);
    CHECK(hoed.energy_spent > 0);
    CHECK(w.energy == kMaxEnergy - hoed.energy_spent);
    CHECK(!use_tool(w, defs, map, Tool::Hoe, 2, 2).ok);       // twice is not work

    CHECK(use_tool(w, defs, map, Tool::Seed, 2, 2, "parsnip").ok);
    CHECK(w.inventory.count(seed_item("parsnip")) && w.inventory[seed_item("parsnip")] == 2);
    CHECK(!use_tool(w, defs, map, Tool::Seed, 2, 2, "parsnip").ok);   // already planted
    CHECK(!use_tool(w, defs, map, Tool::Seed, 3, 3, "turnip").ok);    // no such crop

    CHECK(use_tool(w, defs, map, Tool::Water, 2, 2).ok);
    CHECK(!use_tool(w, defs, map, Tool::Water, 2, 2).ok);
    CHECK(w.at(2, 2) && w.at(2, 2)->watered);

    CHECK(!use_tool(w, defs, map, Tool::Harvest, 2, 2).ok);   // not ready on day 1

    // Energy runs out, and when it does nothing works. That is the resource the whole
    // day is spent against, so "no energy" must be a hard stop, not a slow one.
    w.energy = 0;
    CHECK(!use_tool(w, defs, map, Tool::Hoe, 3, 3).ok);
    CHECK(use_tool(w, defs, map, Tool::Hoe, 3, 3).message.find("tired") != std::string::npos);
}

static void test_crops_ripen_on_schedule() {
    const Defs defs = load_defs();
    const tilemap::Map map = field();
    World w;
    w.seed = 7;
    w.inventory[seed_item("parsnip")] = 1;

    CHECK(use_tool(w, defs, map, Tool::Hoe, 1, 1).ok);
    CHECK(use_tool(w, defs, map, Tool::Seed, 1, 1, "parsnip").ok);

    // A parsnip is days=4 / stages=5: watered every day, it is ripe on the fourth.
    for (int day = 1; day <= 4; ++day) {
        CHECK(use_tool(w, defs, map, Tool::Water, 1, 1).ok);
        const DayReport r = end_day(w, defs);
        CHECK(r.day == day + 1);
        CHECK(w.energy == kMaxEnergy);              // sleeping restores everything
        CHECK(w.minute == kDayStartMin);
        CHECK(w.at(1, 1) && !w.at(1, 1)->watered);  // yesterday's water does not count
        const bool ripe = w.at(1, 1)->stage == defs.crop("parsnip")->stages - 1;
        CHECK(ripe == (day == 4));
    }
    CHECK(use_tool(w, defs, map, Tool::Harvest, 1, 1).ok);
    CHECK(w.inventory["parsnip"] == 1);
    // Harvesting leaves TILLED soil: replanting should not need a second hoe swing.
    CHECK(w.at(1, 1) && w.at(1, 1)->tilled && w.at(1, 1)->crop == -1);

    // A crop whose stage count does not divide its day count must still ripen on the
    // day the definition says. Deriving "ripe" from a rounded stage index made this
    // one harvestable a day early, and the parsnip (4 days / 5 stages, which divide
    // evenly) could never have shown it.
    {
        const auto slow = parse_defs("crop pumpkin days=5 stages=3 sell=90 seed=50\n");
        CHECK(slow.has_value());
        World p;
        p.inventory[seed_item("pumpkin")] = 1;
        CHECK(use_tool(p, *slow, map, Tool::Hoe, 2, 2).ok);
        CHECK(use_tool(p, *slow, map, Tool::Seed, 2, 2, "pumpkin").ok);
        for (int day = 1; day <= 5; ++day) {
            CHECK(use_tool(p, *slow, map, Tool::Water, 2, 2).ok);
            const DayReport r = end_day(p, *slow);
            const bool ripe = p.at(2, 2)->stage == 2;
            CHECK(ripe == (day == 5));
            CHECK(r.crops_grown == (day == 5 ? 1 : 0));
            CHECK(use_tool(p, *slow, map, Tool::Harvest, 2, 2).ok == (day == 5));
        }
        // ...and the intermediate stages actually move, so the crop is not invisible
        // for four days and then suddenly a pumpkin.
        World q;
        q.inventory[seed_item("pumpkin")] = 1;
        use_tool(q, *slow, map, Tool::Hoe, 2, 2);
        use_tool(q, *slow, map, Tool::Seed, 2, 2, "pumpkin");
        std::vector<int> seen;
        for (int day = 1; day <= 5; ++day) {
            use_tool(q, *slow, map, Tool::Water, 2, 2);
            end_day(q, *slow);
            seen.push_back(q.at(2, 2)->stage);
        }
        CHECK(seen.front() < seen.back());
        CHECK(seen.back() == 2);
    }

    // An unwatered day is a lost day, not a slower one.
    World dry;
    dry.inventory[seed_item("parsnip")] = 1;
    CHECK(use_tool(dry, defs, map, Tool::Hoe, 1, 1).ok);
    CHECK(use_tool(dry, defs, map, Tool::Seed, 1, 1, "parsnip").ok);
    for (int i = 0; i < 4; ++i) end_day(dry, defs);
    CHECK(dry.at(1, 1)->stage == 0);
    CHECK(!use_tool(dry, defs, map, Tool::Harvest, 1, 1).ok);
}

static void test_clock_and_collapse() {
    World w;
    CHECK(w.time_text() == "06:00");

    // A frame is far shorter than a game minute. Truncating per call would stop time
    // altogether, so the remainder has to accumulate.
    for (int i = 0; i < 36; ++i) CHECK(!advance(w, 1.0 / 60.0));
    CHECK(w.minute == kDayStartMin + 1);
    CHECK(w.time_text() == "06:01");

    // The same elapsed time reaches the same minute at any frame rate.
    World a, b;
    for (int i = 0; i < 600; ++i) advance(a, 1.0 / 60.0);      // 10 s at 60 fps
    for (int i = 0; i < 300; ++i) advance(b, 1.0 / 30.0);      // 10 s at 30 fps
    CHECK(a.minute == b.minute);

    // 02:00 is the wall. It reports ONCE, so a caller cannot be sent to bed twice.
    World late;
    late.minute = kCollapseMin - 1;
    CHECK(advance(late, 60.0));
    CHECK(late.collapsed());
    CHECK(!advance(late, 60.0));
    CHECK(late.minute == kCollapseMin);

    const Defs defs = load_defs();
    const DayReport r = end_day(late, defs, /*collapsed*/ true);
    CHECK(r.collapsed);
    CHECK(late.energy < kMaxEnergy);          // collapsing costs the morning
    CHECK(late.energy > 0);
}

static void test_shipping_is_deterministic() {
    const Defs defs = load_defs();
    const auto run = [&defs](std::uint64_t seed) {
        World w;
        w.seed = seed;
        w.shipped["parsnip"] = 10;
        end_day(w, defs);
        return w.gold;
    };
    CHECK(run(1) == run(1));                  // same seed, same money
    CHECK(run(1) != run(2) || run(1) != run(3));   // and the seed actually matters
    // The jitter is small: a day's takings must not swing wildly for no reason.
    const int base = 10 * 35;
    CHECK(run(1) >= base - 20 && run(1) <= base + 20);
}

static void test_save_round_trip() {
    const Defs defs = load_defs();
    const tilemap::Map map = field();
    World w;
    w.seed = 99;
    w.inventory[seed_item("parsnip")] = 2;
    use_tool(w, defs, map, Tool::Hoe, 3, 4);
    use_tool(w, defs, map, Tool::Seed, 3, 4, "parsnip");
    use_tool(w, defs, map, Tool::Water, 3, 4);
    end_day(w, defs);
    w.shipped["parsnip"] = 2;
    advance(w, 120.0);

    const std::string text = doc::to_text(to_save(w));
    std::string why;
    const auto loaded = doc::load_save(text, "farm", kSaveVersion, migrations(), &why);
    CHECK(loaded.has_value());
    if (!loaded) { std::printf("  why: %s\n", why.c_str()); return; }
    const auto back = from_save(*loaded);
    CHECK(back.has_value());
    if (!back) return;

    // The hash is the claim: everything that affects play survived the trip.
    CHECK(hash(*back) == hash(w));
    CHECK(back->at(3, 4) && back->at(3, 4)->crop == defs.crop_index("parsnip"));
    CHECK(back->inventory.at(seed_item("parsnip")) == 1);
    CHECK(back->shipped.at("parsnip") == 2);

    // Writing the same state twice produces the same bytes, so a save can be diffed
    // and content-hashed like everything else in this project.
    CHECK(doc::to_text(to_save(*back)) == text);

    // A save from a NEWER build is refused rather than read with its unknown fields
    // dropped — reading and rewriting is data loss wearing compatibility's clothes.
    doc::SaveState future = to_save(w);
    future.version = kSaveVersion + 5;
    CHECK(!doc::load_save(doc::to_text(future), "farm", kSaveVersion, migrations(), &why));
    CHECK(why.find("newer") != std::string::npos);
    // ...and so is another game's save.
    CHECK(!doc::load_save(text, "creatures", kSaveVersion, migrations(), &why));
}

static void test_migration_chain() {
    // Version 1 has no migrations yet, so the seam is tested with a synthetic chain:
    // the first real migration must be an addition, not a design.
    doc::SaveState old;
    old.game = "farm";
    old.version = 1;
    old.set("gold", 10);
    std::vector<doc::Migration> chain{
        {1, [](doc::SaveState& s) { s.set("gold", s.num("gold") * 2); }},
        {2, [](doc::SaveState& s) { s.set("greeting", "hello"); }},
    };
    std::string why;
    const auto up = doc::load_save(doc::to_text(old), "farm", 3, chain, &why);
    CHECK(up.has_value());
    if (up) {
        CHECK(up->version == 3);
        CHECK(up->num("gold") == 20);
        CHECK(up->var("greeting") == "hello");
    }
    // A gap is refused rather than skipped: silently jumping a version leaves the data
    // in a shape nothing has ever written.
    std::vector<doc::Migration> gap{{2, [](doc::SaveState&) {}}};
    CHECK(!doc::load_save(doc::to_text(old), "farm", 3, gap, &why));
    CHECK(why.find("no migration") != std::string::npos);
}

static void test_schedule_and_npcs() {
    const auto s = parse_schedule(
        "sched 1\n"
        "npc anna\n"
        "at 17:00 anna_shop\n"          // deliberately out of order
        "at 06:00 anna_home\n"
        "# comment\n");
    CHECK(s.has_value());
    if (!s) return;
    CHECK(s->npc == "anna");
    CHECK(s->entries.size() == 2);
    CHECK(s->entries[0].minute == 6 * 60);      // sorted on parse

    CHECK(s->place_at(6 * 60) == "anna_home");
    CHECK(s->place_at(12 * 60) == "anna_home");
    CHECK(s->place_at(17 * 60) == "anna_shop");
    CHECK(s->place_at(23 * 60) == "anna_shop");
    // Before the first entry an NPC is where they were last, not nowhere.
    CHECK(s->place_at(1 * 60) == "anna_shop");

    CHECK(!parse_schedule("sched 9\n").has_value());       // a future version
    CHECK(!parse_schedule("at 06:00 home\n").has_value()); // no magic
    CHECK(!parse_schedule("sched 1\nat 6h00 home\n").has_value());
    CHECK(!parse_schedule("sched 1\nat 06:99 home\n").has_value());

    World w;
    w.npcs.push_back(NpcState{"anna", "", 0, 0});
    const tilemap::Map map = field();
    w.minute = 8 * 60;
    update_npcs(w, map, {*s});
    CHECK(w.npcs[0].x == 1 && w.npcs[0].y == 1);
    w.minute = 18 * 60;
    update_npcs(w, map, {*s});
    CHECK(w.npcs[0].x == 5 && w.npcs[0].y == 2);

    // A place the map does not name leaves the NPC where they were, rather than
    // teleporting them to the origin — which is what a silent lookup failure looks
    // like on screen.
    const auto missing = parse_schedule("sched 1\nnpc anna\nat 06:00 nowhere\n");
    CHECK(missing.has_value());
    update_npcs(w, map, {*missing});
    CHECK(w.npcs[0].x == 5 && w.npcs[0].y == 2);
}

static void test_movement() {
    const tilemap::Map map = field();
    World w;
    w.px = 6; w.py = 2;
    CHECK(!try_move(w, map, 1, 0));       // into the wall
    CHECK(w.px == 6);
    CHECK(try_move(w, map, -1, 0));
    CHECK(w.px == 5);
    w.px = 0;
    CHECK(!try_move(w, map, -1, 0));      // off the map is solid
}

static void test_dialogue() {
    const char* script =
        "dlg 1\n"
        "node start\n"
        "  say Anna Morning!\n"
        "  say Anna The parsnips like water.\n"
        "  choice Ask about the shop -> shop\n"
        "  choice Say goodbye -> bye\n"
        "node shop\n"
        "  say Anna Pierre opens at nine.\n"
        "  goto bye\n"
        "node bye\n"
        "  say Anna See you around.\n"
        "  end\n";
    const auto d = parse_dialogue(script);
    CHECK(d.has_value());
    if (!d) return;
    CHECK(d->nodes.size() == 3);

    DialogueRunner r;
    CHECK(r.begin(*d));
    CHECK(!r.finished());
    CHECK(r.speaker() == "Anna");
    CHECK(r.text() == "Morning!");
    // Choices stay hidden until the last line is on screen: answering a question you
    // have not finished hearing is not a choice.
    CHECK(r.choices().empty());
    r.advance();
    CHECK(r.text() == "The parsnips like water.");
    CHECK(r.choices().size() == 2);
    r.advance();                             // advance does nothing while choosing
    CHECK(r.text() == "The parsnips like water.");

    r.choose(9);                             // out of range is ignored, not a crash
    CHECK(r.text() == "The parsnips like water.");
    r.choose(0);
    CHECK(r.text() == "Pierre opens at nine.");
    r.advance();                             // follows the goto
    CHECK(r.text() == "See you around.");
    r.advance();
    CHECK(r.finished());

    // The other branch reaches the same ending.
    DialogueRunner r2;
    r2.begin(*d);
    r2.advance();
    r2.choose(1);
    CHECK(r2.text() == "See you around.");

    // A node with no way out strands the player in the box.
    CHECK(!parse_dialogue("dlg 1\nnode start\n  say A hi\n").has_value());
    // A jump to a node that does not exist is caught at PARSE time, not when a player
    // happens to pick that option.
    CHECK(!parse_dialogue("dlg 1\nnode start\n say A hi\n goto nope\n").has_value());
    CHECK(!parse_dialogue("dlg 1\nnode start\n say A hi\n choice go -> nope\n").has_value());
    CHECK(!parse_dialogue("dlg 1\n say A outside any node\n").has_value());
    CHECK(!parse_dialogue("dlg 1\nnode start\n shout A hi\n end\n").has_value());
    CHECK(!parse_dialogue("dlg 2\nnode start\n end\n").has_value());
}

static void test_three_days_are_reproducible() {
    const Defs defs = load_defs();
    const tilemap::Map map = field();
    const auto play = [&](std::uint64_t seed) {
        World w;
        w.seed = seed;
        w.inventory[seed_item("parsnip")] = 6;
        for (int day = 0; day < 3; ++day) {
            for (int i = 0; i < 2; ++i) {
                const int x = 1 + i, y = 1 + day;
                use_tool(w, defs, map, Tool::Hoe, x, y);
                use_tool(w, defs, map, Tool::Seed, x, y, "parsnip");
                use_tool(w, defs, map, Tool::Water, x, y);
            }
            for (int f = 0; f < 60 * 60; ++f) advance(w, 1.0 / 60.0);
            w.shipped["parsnip"] = day;
            end_day(w, defs);
        }
        return w;
    };
    const World a = play(1234);
    const World b = play(1234);
    CHECK(hash(a) == hash(b));
    CHECK(a.gold == b.gold);
    CHECK(hash(a) != hash(play(5678)));
    CHECK(a.day == 4);
    CHECK(a.soil.size() == 6);
}


// -----------------------------------------------------------------------------
//  Overrides: the same text, arriving from a dashboard instead of a file.
//  The claim under test is that an override touches ONLY what it names — which is
//  exactly what merge_defs does not do, and why this function had to exist.
// -----------------------------------------------------------------------------
static void test_overrides() {
    Defs d = *parse_defs(
        "crop parsnip season=spring days=6 stages=5 sell=35 seed=20\n"
        "crop kale    season=summer days=9 stages=4 sell=60 seed=40\n"
        "item hoe type=tool tier=1\n");

    // A price change is a price change. The five other fields survive it — this is
    // the whole reason apply_overrides is not merge_defs.
    OverrideReport r = apply_overrides(d, "crop parsnip sell=70\n");
    CHECK(r.applied == 1);
    CHECK(r.problems.empty());
    CHECK(d.crop("parsnip")->sell   == 70);
    CHECK(d.crop("parsnip")->days   == 6);      // NOT reset to the struct default (4)
    CHECK(d.crop("parsnip")->stages == 5);
    CHECK(d.crop("parsnip")->seed   == 20);
    CHECK(d.crop("parsnip")->season == "spring");
    CHECK(d.crop("kale")->sell == 60);          // untouched record

    // Several fields, several records, one blob — the shape a live event payload has.
    r = apply_overrides(d, "crop parsnip sell=100 days=3\ncrop kale sell=90\nitem hoe tier=3\n");
    CHECK(r.applied == 4);
    CHECK(r.problems.empty());
    CHECK(d.crop("parsnip")->sell == 100 && d.crop("parsnip")->days == 3);
    CHECK(d.crop("kale")->sell == 90);
    CHECK(d.item("hoe")->tier == 3);

    // A typo is a typo: reported, and NOTHING from that line lands. The sell=5 in
    // front of the bad field must not survive, or the operator gets a price they can
    // see and a field they think they set.
    r = apply_overrides(d, "crop parsnip sell=5 dayz=1\n");
    CHECK(r.applied == 0);
    CHECK(r.problems.size() == 1);
    CHECK(d.crop("parsnip")->sell == 100);      // the earlier value, untouched

    r = apply_overrides(d, "crop parsnip sell=abc\n");
    CHECK(r.applied == 0 && r.problems.size() == 1);
    CHECK(d.crop("parsnip")->sell == 100);

    // Remote config cannot brick the game: a crop that never grows is refused here,
    // not discovered on the field with a division by zero.
    r = apply_overrides(d, "crop parsnip stages=1\n");
    CHECK(r.applied == 0 && r.problems.size() == 1);
    CHECK(d.crop("parsnip")->stages == 5);
    r = apply_overrides(d, "crop parsnip days=0\n");
    CHECK(r.applied == 0 && r.problems.size() == 1);
    CHECK(d.crop("parsnip")->days == 3);

    // An override NAMES something that exists. Inventing a crop from a dashboard
    // would ship content no client has a seed item or a sprite for.
    r = apply_overrides(d, "crop turnip sell=10\n");
    CHECK(r.applied == 0 && r.problems.size() == 1);
    CHECK(d.crop("turnip") == nullptr);
    CHECK(d.crops.size() == 2);

    // Comments and blank lines behave like the file they came from.
    r = apply_overrides(d, "# festival\n\ncrop kale sell=120   # double\n");
    CHECK(r.applied == 1 && r.problems.empty());
    CHECK(d.crop("kale")->sell == 120);

    // Nothing at all is not an error, it is a quiet Tuesday.
    r = apply_overrides(d, "");
    CHECK(r.applied == 0 && r.problems.empty());
}

// -----------------------------------------------------------------------------
//  Which save wins. The one operation in this project that can destroy work by
//  succeeding, so the whole table is pinned rather than the happy path.
// -----------------------------------------------------------------------------
static void test_sync_decision() {
    // Fresh install, nothing anywhere.
    CHECK(decide_sync(LocalSave{}, RemoteSave{}) == Sync::InSync);

    // Played offline, never uploaded.
    CHECK(decide_sync(LocalSave{true, 111, 0, 0}, RemoteSave{}) == Sync::Push);

    // New machine, cloud has a save.
    CHECK(decide_sync(LocalSave{}, RemoteSave{true, 4, 222}) == Sync::Pull);

    // Byte-identical: content decides, not the version counter. Another machine
    // saving the same world must not make this one upload it again.
    CHECK(decide_sync(LocalSave{true, 999, 1, 999}, RemoteSave{true, 7, 999}) == Sync::InSync);

    // Agreed at v4/hash 222; this machine has played since. Cloud has not moved.
    CHECK(decide_sync(LocalSave{true, 333, 4, 222}, RemoteSave{true, 4, 222}) == Sync::Push);

    // Agreed at v4; the cloud moved to v5 and this machine did not play.
    CHECK(decide_sync(LocalSave{true, 222, 4, 222}, RemoteSave{true, 5, 555}) == Sync::Pull);

    // BOTH moved. The only honest answer is to ask.
    CHECK(decide_sync(LocalSave{true, 333, 4, 222}, RemoteSave{true, 5, 555}) == Sync::Conflict);

    // The bookmark says nobody moved, yet the bytes differ — impossible, so it is
    // reported rather than resolved in local's favour by accident.
    CHECK(decide_sync(LocalSave{true, 222, 4, 222}, RemoteSave{true, 4, 555}) == Sync::Conflict);

    // A machine that has never synced but has both saves cannot claim "unchanged":
    // synced_hash 0 differs from any real hash, so it is a conflict, not a silent push.
    CHECK(decide_sync(LocalSave{true, 333, 0, 0}, RemoteSave{true, 2, 555}) == Sync::Conflict);

    CHECK(std::string(sync_text(Sync::Conflict)) != std::string(sync_text(Sync::InSync)));
}


// -----------------------------------------------------------------------------
//  One number, one place. `sell` used to be written in BOTH crops.def and
//  items.def with the same value, and end_day read the item's copy — so changing
//  the crop's price, from a file or from a dashboard, did nothing at all.
// -----------------------------------------------------------------------------
static void test_the_crop_owns_the_price() {
    Defs d = *parse_defs("crop parsnip season=spring days=2 stages=3 sell=35 seed=20\n"
                         "item parsnip type=crop sell=999\n");
    const auto earn = [](const Defs& defs) {
        World w;
        w.seed = 7;
        w.shipped["parsnip"] = 1;
        return end_day(w, defs).gold_earned;
    };
    CHECK(earn(d) >= 33 && earn(d) <= 37);          // the crop's 35, not the item's 999

    // ...and that is the number a remote override moves.
    CHECK(apply_overrides(d, "crop parsnip sell=100\n").applied == 1);
    CHECK(earn(d) >= 98 && earn(d) <= 102);

    // A non-crop item still prices itself: the rule is "the crop owns a CROP's price",
    // not "items have no prices".
    Defs t = *parse_defs("item parsnip_seed type=seed sell=10\n");
    World w;
    w.seed = 7;
    w.shipped["parsnip_seed"] = 1;
    const int g = end_day(w, t).gold_earned;
    CHECK(g >= 8 && g <= 12);
}


// -----------------------------------------------------------------------------
//  The theme: which picture a semantic tile id wears. The load-bearing claim is
//  that an id with NO line has no art — that is what lets a pack cover part of a
//  map instead of all of it.
// -----------------------------------------------------------------------------
static void test_theme() {
    const auto t = parse_theme(
        "# a comment\n"
        "sheet town  textures/town.hrt       16\n"
        "sheet water textures/farm_water.hrt 16\n"
        "\n"
        "tile ground 1 town  0     # grass\n"
        "tile ground 2 town  40\n"
        "tile ground 3 water 0\n"
        "tile decor 1 town 28\n");
    CHECK(t.has_value());
    if (!t) return;
    CHECK(t->sheets.size() == 2);
    CHECK(t->sheets.at("town").path == "textures/town.hrt");
    CHECK(t->sheets.at("town").tile == 16);
    CHECK(t->sheets.at("water").path == "textures/farm_water.hrt");

    // The join, and the reason there are two sheets at all: two ids in the SAME layer
    // resolve to different files. Grass comes from the imported pack, the pond from a
    // tile this project drew, and the map does not know or care which.
    const farm::Theme::Art* grass = t->find("ground", 1);
    const farm::Theme::Art* pond  = t->find("ground", 3);
    CHECK(grass && grass->sheet == "town"  && grass->index == 0);   // index 0 is a TILE
    CHECK(pond  && pond->sheet  == "water" && pond->index  == 0);   // ...in a different file
    CHECK(t->find("ground", 2)->index == 40);
    CHECK(t->find("decor", 1)->index == 28);

    // Unmapped: a different id, a different layer, an id nobody listed.
    CHECK(t->find("ground", 4) == nullptr);
    CHECK(t->find("decor", 2) == nullptr);
    CHECK(t->find("nosuch", 1) == nullptr);

    // The tile size defaults rather than failing, because 16 is what every pack in
    // sight uses and a missing number is not a missing decision.
    const auto d = parse_theme("sheet s a.hrt\ntile ground 1 s 0\n");
    CHECK(d && d->sheets.at("s").tile == 16);

    // A theme that declares a sheet and maps nothing is legal: it means "no art yet",
    // which is a real state a project passes through.
    CHECK(parse_theme("sheet s a.hrt\n").has_value());

    // Refusals. A theme is small and hand-written; a typo here is a typo, not a
    // future field, so unlike a defs FILE an unknown record is an error.
    CHECK(!parse_theme(""));                                    // no sheet
    CHECK(!parse_theme("tile ground 1 s 0\n"));                 // ...even with tiles
    CHECK(!parse_theme("sheet s a.hrt\nwibble 1 2\n"));
    CHECK(!parse_theme("sheet s a.hrt\ntile ground\n"));
    CHECK(!parse_theme("sheet s a.hrt\ntile ground 1 s\n"));    // no index
    CHECK(!parse_theme("sheet s a.hrt\ntile ground 0 s 5\n"));  // id 0 is "empty"
    CHECK(!parse_theme("sheet s a.hrt\ntile ground 1 s -2\n"));
    CHECK(!parse_theme("sheet s a.hrt 0\n"));
    CHECK(!parse_theme("sheet s\n"));                           // a name and no path

    // The two refusals this format introduced, and the reason they are refusals.
    //
    // A `tile` line naming a sheet nobody declared is a typo that would otherwise be
    // INDISTINGUISHABLE from "this id has no art yet" — the tile would silently keep
    // its flat colour and the file would look correct.
    CHECK(!parse_theme("sheet town a.hrt\ntile ground 1 twon 0\n"));

    // Two sheets claiming one name: one of them loses, and which one depends on line
    // order. Nothing about that is a decision anybody made.
    CHECK(!parse_theme("sheet s a.hrt\nsheet s b.hrt\n"));
}

// -----------------------------------------------------------------------------
//  Provenance: the pond tile in the repo is what its recipe says it is.
//
//  The Texture Lab writes a `.recipe` beside every `.hrt` so a texture can be
//  RE-EDITED. That sidecar is also evidence — but only if something checks it.
//  Until this test, "drawn in the Studio" was a sentence in a comment; the bytes
//  could have come from anywhere and nobody would know.
// -----------------------------------------------------------------------------
static void test_water_provenance() {
    assets::set_base_path(ASSET_ROOT "/assets");

    const auto recipe = assets::load_file("textures/farm_water.recipe");
    const auto baked  = assets::load_file("textures/farm_water.hrt");
    CHECK(recipe && baked);
    if (!recipe || !baked) return;

    int applied = 0;
    const studio::TextureParams p =
        studio::from_recipe(std::string(recipe->begin(), recipe->end()), &applied);
    CHECK(applied == 12);            // every key the format has, so nothing defaulted silently
    CHECK(p.size == 16);             // one tile, the size the theme cuts at

    // Byte-for-byte. The generator is documented as deterministic and pure; if that
    // ever stops being true this is where it is found, and a texture that cannot be
    // regenerated is a texture that cannot be edited.
    CHECK(gfx::encode_hrt(studio::generate(p)) == *baked);

    // Two colours and no more. Kenney's tiles are FLAT colour, and a smooth gradient
    // beside them looked like a different game — the threshold is the whole reason
    // procedural noise can sit next to hand-drawn pixel art.
    const auto img = gfx::decode_hrt(*baked);
    CHECK(img.has_value());
    if (!img) return;
    std::set<gfx::Color> shades(img->pixels.begin(), img->pixels.end());
    CHECK(shades.size() == 2);
    CHECK(img->w == 16 && img->h == 16);
}


// -----------------------------------------------------------------------------
//  The on-screen controls. The load-bearing claim is not "a tap moves you" — it is
//  that ONE layout function answers both the renderer and the hit test, and that the
//  pointer being over a control STOPS the world from also reading it.
// -----------------------------------------------------------------------------
static void test_controls() {
    const farm::Layout l = farm::layout(1280, 720);
    CHECK(l.visible());

    // Every control is on screen and none of them overlap. Two buttons sharing a
    // pixel is a coin toss the player always loses, and it is invisible in a mockup.
    const farm::Box* all[] = {&l.up, &l.down, &l.left, &l.right, &l.use, &l.seed};
    for (const farm::Box* a : all) {
        CHECK(a->x >= 0 && a->y >= 0);
        CHECK(a->x + a->w <= 1280 && a->y + a->h <= 720);
        CHECK(a->w >= 44 && a->h >= 44);      // reachable by a thumb, not by aiming
        for (const farm::Box* b : all) {
            if (a == b) continue;
            const bool overlap = a->x < b->x + b->w && b->x < a->x + a->w &&
                                 a->y < b->y + b->h && b->y < a->y + a->h;
            CHECK(!overlap);
        }
    }
    // The d-pad is a cross: left is left of right, up is above down, and they share a
    // column and a row. Written out because "it looked fine" is how a mirrored pad
    // ships.
    CHECK(l.left.x < l.right.x);
    CHECK(l.up.y < l.down.y);
    CHECK(l.up.x == l.down.x);
    CHECK(l.left.y == l.right.y);
    // ...and the actions are on the far side from the d-pad, or one thumb does both.
    CHECK(l.use.x > l.right.x);

    // Held direction, like an arrow key.
    const auto at = [](const farm::Box& b, bool down, bool pressed) {
        return farm::Pointer{b.x + b.w / 2, b.y + b.h / 2, down, pressed};
    };
    farm::Action a = farm::read(l, at(l.right, true, true));
    CHECK(a.dx == 1 && a.dy == 0);
    CHECK(a.consumed);
    a = farm::read(l, at(l.up, true, true));
    CHECK(a.dy == -1 && a.dx == 0);

    // The actions are EDGES. Holding `use` must not fire every frame — that would
    // hoe a tile sixty times a second and drain a day of energy in one press.
    a = farm::read(l, at(l.use, true, true));
    CHECK(a.use);
    a = farm::read(l, at(l.use, true, false));
    CHECK(!a.use);
    CHECK(a.consumed);                        // ...but it still blocks the world

    // Consumed is by POSITION, not by the button being down: a pointer resting over
    // the pad must not highlight the tile beneath it either.
    a = farm::read(l, at(l.seed, false, false));
    CHECK(a.consumed);
    CHECK(!a.seed && a.dx == 0 && a.dy == 0);

    // Off the controls, and off the screen entirely.
    a = farm::read(l, farm::Pointer{640, 300, true, true});
    CHECK(!a.consumed && a.dx == 0 && !a.use);
    a = farm::read(l, farm::Pointer{-1, -1, true, true});
    CHECK(!a.consumed);

    // A screen too small to hold the pad AND leave the world visible draws nothing,
    // and then nothing is consumed — the keyboard is the only honest answer there,
    // and a control covering what it acts on is worse than one that is absent.
    const farm::Layout tiny = farm::layout(320, 200);
    CHECK(!tiny.visible());
    CHECK(!farm::read(tiny, farm::Pointer{10, 10, true, true}).consumed);

    // The retro 480x270 framebuffer the engine demo uses is deliberately in the "too
    // small" case; the farm's own 1280x720 is not. Both are pinned so a change to the
    // threshold has to be a decision.
    CHECK(!farm::layout(480, 270).visible());
    CHECK(farm::layout(960, 600).visible());
}

int main() {
    test_defs();
    test_theme();
    test_water_provenance();
    test_controls();
    test_the_crop_owns_the_price();
    test_overrides();
    test_sync_decision();
    test_a_day_of_farming();
    test_crops_ripen_on_schedule();
    test_clock_and_collapse();
    test_shipping_is_deterministic();
    test_save_round_trip();
    test_migration_chain();
    test_schedule_and_npcs();
    test_movement();
    test_dialogue();
    test_three_days_are_reproducible();
    if (g_failures == 0) std::printf("farm: all tests passed\n");
    else                 std::printf("farm: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
