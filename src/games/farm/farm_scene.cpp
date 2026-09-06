// =============================================================================
//  games/farm/farm_scene.cpp
// =============================================================================
#include "games/farm/farm_scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <random>
#include <sstream>
#include <utility>

#include "engine/assets.hpp"
#include "engine/document/document.hpp"
#include "engine/image.hpp"
#include "engine/renderer2d.hpp"
#include "engine/tilemap/autotile.hpp"
#include "engine/ui/theme.hpp"

namespace farm {

namespace th = ui::theme;

namespace {

// The farm is its own BaaS project, not a corner of the colony's: two games sharing
// one project would share a save slot, a leaderboard and a config namespace, and the
// first collision would look like a bug in whichever game noticed second.
constexpr const char* kApiKey    = "pk_demo_farm";
constexpr const char* kSaveSlot  = "farm";
constexpr const char* kConfigKey = "farm_defs";

// A flat, distinctive fill: the strip behind a remote-config problem. Named because a
// test counts exactly these pixels to prove the message is on screen at all.
constexpr gfx::Color kProblemChip = 0xFF301A20;

constexpr int    kTile = 16;
constexpr double kStepSeconds = 0.12;      // one tile per this long while held
constexpr double kTypeCharsPerSecond = 45.0;

// The game's own palette. Deliberately NOT the Studio's editor palette: an editor
// colours tile IDS so an author can tell them apart, a game colours the WORLD.
constexpr gfx::Color kGrass  = 0xFF4E7A3C;
constexpr gfx::Color kPath   = 0xFF9A7B4F;
constexpr gfx::Color kWater  = 0xFF2E6E8E;
constexpr gfx::Color kTree   = 0xFF23482A;
constexpr gfx::Color kWall   = 0xFF7A5B44;
constexpr gfx::Color kRock   = 0xFF6B7180;
constexpr gfx::Color kTilled = 0xFF6B4A2F;
constexpr gfx::Color kWet    = 0xFF4A3520;
constexpr gfx::Color kPlayer = 0xFFF2CC8F;
constexpr gfx::Color kNpc    = 0xFFE07A5F;

gfx::Color ground_color(std::int32_t id) {
    switch (id) {
        case 2:  return kPath;
        case 3:  return kWater;
        default: return kGrass;
    }
}

gfx::Color decor_color(std::int32_t id) {
    switch (id) {
        case 1:  return kTree;
        case 2:  return kWall;
        default: return kRock;
    }
}

std::string read_text(const std::string& path) {
    auto b = assets::load_file(path);
    return b ? std::string(b->begin(), b->end()) : std::string();
}

// Night is a tint, not a light model: alpha over the world, peaking around 01:00.
// Cheap, readable, and it makes the clock matter without a lighting pass.
int night_alpha(int minute) {
    // 06:00 clear -> 18:00 dusk -> 26:00 (02:00) darkest.
    if (minute <= 17 * 60) return 0;
    const int span = kCollapseMin - 17 * 60;
    const int into = std::min(minute - 17 * 60, span);
    return 150 * into / std::max(1, span);
}

} // namespace

gbaas::Config FarmScene::default_config() { return {gbaas::default_base_url(), kApiKey}; }

FarmScene::FarmScene(gbaas::Config cfg, std::unique_ptr<gbaas::ITransport> transport)
    : client_(std::move(cfg),
              transport ? std::move(transport) : gbaas::make_default_transport()) {
    load();
    connect();
}

void FarmScene::load() {
    ready_ = false;
    problem_.clear();

    const std::string map_text = read_text("maps/farm_home.map2");
    if (map_text.empty()) { problem_ = "missing maps/farm_home.map2"; return; }
    auto m = tilemap::load(map_text);
    if (!m) { problem_ = "maps/farm_home.map2 did not parse"; return; }
    map_ = std::move(*m);

    auto crops = parse_defs(read_text("farm/crops.def"));
    auto items = parse_defs(read_text("farm/items.def"));
    if (!crops || !items) { problem_ = "farm/*.def did not parse"; return; }
    defs_ = *crops;
    merge_defs(defs_, *items);
    if (defs_.crops.empty()) { problem_ = "farm/crops.def defines no crops"; return; }

    if (auto s = parse_schedule(read_text("farm/npc/anna.sched"))) schedules_.push_back(*s);
    if (auto d = parse_dialogue(read_text("farm/npc/anna.dlg"))) { anna_ = *d; have_anna_ = true; }

    world_ = World{};
    world_.seed = 20260904;
    if (const tilemap::Entity* spawn = map_.entity("spawn_player")) {
        world_.px = spawn->x;
        world_.py = spawn->y;
    }
    // A starting hand, so the first day is playable rather than a shopping trip that
    // does not exist yet.
    for (const CropDef& c : defs_.crops) world_.inventory[seed_item(c.name)] = 5;
    if (map_.entity("anna")) world_.npcs.push_back(NpcState{"anna", "", 0, 0});
    update_npcs(world_, map_, schedules_);

    // ---- art ----
    // A theme that does not parse, or a sheet that will not load, is not fatal: the
    // farm rendered fine as flat colours for two chapters and still does. It IS
    // reported, because "the art silently did not load" is a bug that looks like a
    // design choice.
    if (auto th = parse_theme(read_text("farm/theme.def"))) {
        theme_ = *th;
        // Every declared sheet gets an entry even when its image will not load, so the
        // draw path has exactly one shape to handle: a sheet that is missing and a
        // sheet that is empty are the same thing by construction, not by a second
        // branch that only one of them takes.
        for (const auto& [name, sh] : theme_.sheets) {
            tilemap::Tileset& into = tiles_[name];
            if (const auto img = gfx::load_image(sh.path)) into = tilemap::Tileset::cut(*img, sh.tile);
            if (into.count() == 0)
                problem_art_ += (problem_art_.empty() ? "" : "; ") + std::string("could not load ") + sh.path;
        }
    } else {
        problem_art_ = "farm/theme.def did not parse";
    }

    read_bookmark();

    cam_.set_deadzone(24.0f, 16.0f);
    cam_.set_smoothing(0.90f);
    cam_.set_bounds(static_cast<float>(map_.w * kTile), static_cast<float>(map_.h * kTile));
    cam_.snap_to(tilemap::Vec2f{static_cast<float>(world_.px * kTile + kTile / 2),
                                static_cast<float>(world_.py * kTile + kTile / 2)});

    // Resume. A game that forgets yesterday every time it starts does not have a save
    // file, it has a screenshot — and until now the only way back in was F9, which
    // nobody presses because nobody knows. It also makes cloud sync coherent: what is
    // on screen and what is on disk are the same world, so uploading one cannot
    // destroy the other.
    if (const std::string text = read_text(save_path()); !text.empty()) {
        std::string why;
        if (const auto w = world_from_text(text, &why)) {
            adopt_world(*w);
            say("day " + std::to_string(world_.day) + " - welcome back", 3.0);
        } else {
            // Bytes we cannot read are still someone's farm. Keep a copy under a name
            // nothing overwrites before the first save of this session lands on it.
            if (const auto raw = assets::load_file(save_path())) assets::write_file(save_path() + ".broken", *raw);
            say("save could not be read - kept a copy, starting fresh", 8.0);
        }
    }
    ready_ = true;
}

// One tile of art, or false when this id has none. The two callers then fall back to
// the flat colour they used to draw — which is how a pack with no water tile still
// themes the grass, rather than the whole map reverting to squares.
// A sheet that was never declared and a sheet whose image would not load look the
// same on purpose: both are a tileset with no tiles. Collapsing them here is what
// lets draw_tile below keep exactly two guards instead of three.
const tilemap::Tileset& FarmScene::sheet_of(const std::string& name) const {
    static const tilemap::Tileset kNone;
    const auto it = tiles_.find(name);
    return it == tiles_.end() ? kNone : it->second;
}

bool FarmScene::draw_tile(gfx::Renderer2D& g, const char* layer, std::int32_t id, int x, int y,
                          int px, int py) const {
    // No `id == 0` check: `parse_theme` refuses to map id 0 at all, so an empty cell
    // can never have a line, and find() returns nullptr for it like any other unmapped
    // id. The invariant lives in the parser; repeating it here was a guard a
    // mutation could delete with nothing noticing.
    const Theme::Art* a = theme_.find(layer, static_cast<int>(id));
    if (!a) return false;
    // ONE condition, three reasons, and none of them re-implemented here: Tileset
    // already answers "no such tile" with a null sprite, and sheet_of already turns a
    // sheet that would not load into an empty one. So a sheet nobody declared, a sheet
    // whose image is missing, and an index past the end of a real sheet all arrive as
    // the same w == 0 — and the tile falls back to flat colour instead of a hole.
    // For a line set `index` is the base of sixteen consecutive pieces and the cell's
    // own neighbours pick which. The out-of-range case needs no new branch: a base
    // that leaves fewer than sixteen tiles in the sheet lands past the end for exactly
    // the pieces that do not exist, and Tileset already answers that with a null
    // sprite — so a half-drawn line set degrades to flat colour cell by cell instead
    // of drawing whatever tile happens to follow it.
    const int index = a->index + (a->autotiled ? line_piece(map_, layer, id, x, y) : 0);
    const gfx::Sprite s = sheet_of(a->sheet).sprite(static_cast<std::size_t>(index));
    if (s.w == 0) return false;
    g.blit(s, px, py);
    return true;
}

void FarmScene::say(std::string msg, double seconds) {
    message_ = std::move(msg);
    message_t_ = seconds;
}

void FarmScene::facing(int& x, int& y) const {
    x = world_.px + face_x_;
    y = world_.py + face_y_;
}

std::string FarmScene::save_path() const { return "saves/farm/slot1.sav"; }

// Text -> World, with no side effects. Three callers need exactly this and nothing
// else: loading from disk, hashing the local save to compare it with the cloud, and
// reading a downloaded copy before deciding whether to keep it. Only the first of
// those wants the world installed, which is why installing is a separate step.
std::optional<World> FarmScene::world_from_text(const std::string& text,
                                                std::string* why) const {
    if (text.empty()) return std::nullopt;
    const auto s = doc::load_save(text, "farm", kSaveVersion, migrations(), why);
    if (!s) return std::nullopt;
    return from_save(*s);
}

// Install a world that came from somewhere else. The NPC list is not in the save: it
// is derived from the map, so a map that gains an NPC does not need every old save
// migrated.
void FarmScene::adopt_world(World w) {
    world_ = std::move(w);
    world_.npcs.clear();
    if (map_.entity("anna")) world_.npcs.push_back(NpcState{"anna", "", 0, 0});
    update_npcs(world_, map_, schedules_);
    cam_.snap_to(tilemap::Vec2f{static_cast<float>(world_.px * kTile + kTile / 2),
                                static_cast<float>(world_.py * kTile + kTile / 2)});
}

void FarmScene::save_game() {
    if (!doc::save(save_path(), doc::to_text(to_save(world_)))) { say("could not save"); return; }
    say("saved");
    push_save();   // the local file is the record; the cloud is a copy of it
}

void FarmScene::load_game() {
    const std::string text = read_text(save_path());
    if (text.empty()) { say("no save yet"); return; }
    std::string why;
    const auto w = world_from_text(text, &why);
    if (!w) { say(why.empty() ? "save could not be read" : why, 5.0); return; }
    adopt_world(*w);
    say("loaded");
}

// ---- cloud ---------------------------------------------------------------------

std::string FarmScene::bookmark_path() const { return "saves/farm/slot1.sync"; }

// The bookmark is metadata ABOUT the save, not part of the world, so it sits beside
// the file instead of inside it. Putting it in the save would change the save format
// (and its hash) to record something the game itself never reads.
void FarmScene::read_bookmark() {
    synced_version_ = 0;
    synced_hash_    = 0;
    std::istringstream in(read_text(bookmark_path()));
    std::string key;
    while (in >> key) {
        if      (key == "version") in >> synced_version_;
        else if (key == "hash")    in >> synced_hash_;
        else                       break;
    }
}

void FarmScene::write_bookmark(long long version, std::uint64_t hash_value) {
    synced_version_ = version;
    synced_hash_    = hash_value;
    const std::string text = "version " + std::to_string(version) + "\nhash " +
                             std::to_string(hash_value) + "\n";
    assets::write_file(bookmark_path(), std::vector<std::uint8_t>(text.begin(), text.end()));
}

LocalSave FarmScene::local_stamp() const {
    LocalSave l;
    l.synced_version = synced_version_;
    l.synced_hash    = synced_hash_;
    std::string why;
    if (const auto w = world_from_text(read_text(save_path()), &why)) {
        l.present = true;
        l.hash    = hash(*w);
    }
    return l;
}

// An opaque id for THIS installation, made once and kept. It is what turns a guest
// from a new account every launch into the same player coming back — without it a
// cloud save is written into an account nothing will ever read again, which is how a
// working upload and a working download can still add up to a feature that does not
// work. Found exactly that way: the end-to-end test pushed a save and could not
// pull it.
//
// Stored beside the saves rather than under farm/, because it identifies the machine,
// not the game. It stays here until a second game wants it — then it moves up, and
// moving it is a rename.
std::string FarmScene::device_id() {
    if (const std::string have = read_text("saves/device.id"); !have.empty()) return have;

    // Mixed from three sources rather than taken straight from std::random_device,
    // which the standard explicitly allows to be a deterministic PRNG — and this value
    // decides WHICH PLAYER you are, so a repeated one means strangers sharing a farm.
    // (The identical ids first seen in the browser turned out not to be this: the build
    // was shipping the developer's own saves/device.id inside the preloaded bundle. It
    // is excluded now. The mixing stays as cheap insurance for a value that must not
    // repeat.)
    std::random_device rd;
    std::uint64_t      h = 0xcbf29ce484222325ull;
    const auto mix = [&h](std::uint64_t v) { h = (h ^ v) * 0x100000001b3ull; };
    for (int i = 0; i < 4; ++i) mix(rd());
    mix(static_cast<std::uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count()));
    mix(static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    mix(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&h)));

    static const char* kHex = "0123456789abcdef";
    std::string        id;
    for (int word = 0; word < 2; ++word) {
        for (int b = 60; b >= 0; b -= 4) id += kHex[(h >> b) & 0xF];
        mix(0x9E3779B97F4A7C15ull);   // a second 64 bits from the same state
    }
    assets::write_file("saves/device.id", std::vector<std::uint8_t>(id.begin(), id.end()));
    return id;
}

void FarmScene::connect() {
    link_       = Link::Connecting;
    cloud_line_ = "connecting";
    client_.auth().guest(device_id(), [this](gbaas::Result<gbaas::Session> r) {
        if (!r) {
            link_       = Link::Failed;
            cloud_line_ = "offline";
            return;      // and the game carries on: nothing above depends on this
        }
        link_       = Link::Online;
        cloud_line_ = "online";
        pull_config();   // ...which chains into sync_saves()
    });
}

// Prices arrive in layers: the file, then remote config, then any live event. The
// layers are CHAINED rather than fired together because they are the same kind of
// edit to the same table — two in flight at once means whichever response lands
// second wins, and the festival would apply or not depending on the network.
void FarmScene::pull_config() {
    client_.config().get(kConfigKey, [this](gbaas::Result<std::string> r) {
        if (r) {
            const OverrideReport rep = apply_overrides(defs_, *r);
            overrides_applied_ += rep.applied;
            if (!rep.problems.empty()) config_problem_ = "config: " + rep.problems.front();
        }
        // A project with no config set is not an error; only a transport failure is,
        // and even that only costs the balance pass, not the game.
        client_.events().active([this](gbaas::Result<std::vector<gbaas::LiveEvent>> er) {
            if (er) {
                for (const gbaas::LiveEvent& e : *er) {
                    const OverrideReport rep = apply_overrides(defs_, e.payload);
                    overrides_applied_ += rep.applied;
                    if (!rep.problems.empty()) config_problem_ = e.key + ": " + rep.problems.front();
                    event_name_ = e.name;
                }
            }
            sync_saves();
        });
    });
}

void FarmScene::sync_saves() {
    syncing_ = true;
    client_.saves().get(kSaveSlot, [this](gbaas::Result<gbaas::Save> r) {
        syncing_ = false;
        RemoteSave remote{};
        if (r) {
            std::string why;
            const auto w = world_from_text(r->data, &why);
            if (!w) {
                // Something is in the slot and we cannot read it. Pushing over it would
                // destroy a save that a newer build might understand perfectly well.
                cloud_line_ = "cloud save unreadable";
                return;
            }
            remote.present = true;
            remote.version = r->version;
            remote.hash    = hash(*w);
            cloud_text_    = r->data;
            cloud_version_ = r->version;
        } else if (r.error && r.error->status != 404) {
            // 404 means the slot is empty — a fact. Anything else means we do not KNOW
            // what is up there, and "do not know" must never turn into an upload.
            cloud_line_ = "cloud unavailable";
            return;
        }

        switch (decide_sync(local_stamp(), remote)) {
            case Sync::InSync:
                cloud_line_ = remote.present ? "cloud in sync" : "cloud empty";
                break;
            case Sync::Push:
                push_save();
                break;
            case Sync::Pull:
                adopt_cloud();
                break;
            case Sync::Conflict:
                conflict_   = true;
                cloud_line_ = "two saves differ";
                say("cloud save differs - F6 keeps yours, F7 takes the cloud's", 10.0);
                break;
        }
    });
}

void FarmScene::push_save() {
    if (link_ != Link::Online) return;
    // A save pressed while the first sync is in flight is not lost: save_game() has
    // already written the file, and the verdict about to arrive reads the file, so it
    // sees this world and pushes it if that is the right answer. Uploading here as
    // well would send it twice and decide from a cloud snapshot older than the upload.
    if (syncing_) { cloud_line_ = "saved - syncing"; return; }
    const std::string   text = doc::to_text(to_save(world_));
    const std::uint64_t h    = hash(world_);
    // Disk first: the bookmark about to be written claims these two agree, and a
    // bookmark that lies is worse than no bookmark at all.
    doc::save(save_path(), text);
    cloud_line_ = "uploading";
    client_.saves().put(kSaveSlot, text, [this, h](gbaas::Result<gbaas::SaveMeta> r) {
        if (!r) { cloud_line_ = "upload failed"; return; }
        write_bookmark(r->version, h);
        conflict_   = false;
        cloud_line_ = "cloud v" + std::to_string(r->version);
    });
}

void FarmScene::adopt_cloud() {
    std::string why;
    const auto  w = world_from_text(cloud_text_, &why);
    if (!w) { cloud_line_ = "cloud save unreadable"; return; }
    doc::save(save_path(), cloud_text_);
    write_bookmark(cloud_version_, hash(*w));
    adopt_world(*w);
    conflict_   = false;
    cloud_line_ = "cloud v" + std::to_string(cloud_version_);
    say("loaded from the cloud", 4.0);
}

// A conflict is the one cloud state the player has to DO something about, so the chip
// stops reporting and starts asking — and names the two keys, because "two saves
// differ" is a sentence that leaves someone staring at a farm they cannot save.
std::string FarmScene::cloud_chip() const {
    return conflict_ ? "F6 keep yours / F7 take cloud" : cloud_line_;
}

void FarmScene::track(const char* name, const std::string& props) {
    if (link_ != Link::Online) return;
    client_.analytics().track(name, props);
}

void FarmScene::sleep_now(bool collapsed) {
    const DayReport r = end_day(world_, defs_);
    last_day_ = r;
    update_npcs(world_, map_, schedules_);
    std::string msg = "day " + std::to_string(r.day);
    if (r.gold_earned > 0) msg += "   +" + std::to_string(r.gold_earned) + "g";
    if (r.crops_grown > 0) msg += "   " + std::to_string(r.crops_grown) + " ready";
    if (collapsed) msg += "   (you passed out)";
    say(msg, 5.0);

    // The day is the game's natural save point, so it is also the cloud's. Analytics
    // go out first: `day_end` is the event an operator actually reads, and it should
    // not depend on whether the upload that follows it worked.
    track("farm.day_end", "{\"day\":" + std::to_string(r.day) +
                          ",\"gold\":" + std::to_string(world_.gold) +
                          ",\"energy\":" + std::to_string(world_.energy) + "}");
    if (r.gold_earned > 0)
        track("farm.sale", "{\"gold\":" + std::to_string(r.gold_earned) + "}");
    if (!conflict_) push_save();   // an unresolved conflict is not a licence to overwrite
    // The report has to be built from the SAME call that advanced the day, or the
    // numbers on screen belong to a world that no longer exists.
    (void)collapsed;
}

void FarmScene::interact() {
    int tx = 0, ty = 0;
    facing(tx, ty);

    // Talking to the NPC in front of you takes priority over farming the tile it is
    // standing on — otherwise you hoe the villager.
    for (const NpcState& n : world_.npcs) {
        if (n.x != tx || n.y != ty) continue;
        if (!have_anna_) { say("Anna nods."); return; }
        talking_ = talk_.begin(anna_);
        choice_ = 0;
        typed_ = 0.0;
        return;
    }

    if (const tilemap::Entity* bed = map_.entity("bed"); bed && bed->x == tx && bed->y == ty) {
        sleep_now(false);
        return;
    }
    if (const tilemap::Entity* box = map_.entity("ship_box"); box && box->x == tx && box->y == ty) {
        int moved = 0;
        for (auto it = world_.inventory.begin(); it != world_.inventory.end();) {
            if (defs_.crop(it->first) != nullptr && it->second > 0) {
                world_.shipped[it->first] += it->second;
                moved += it->second;
                it = world_.inventory.erase(it);
            } else {
                ++it;
            }
        }
        say(moved > 0 ? "shipped " + std::to_string(moved) + " - paid when you sleep"
                      : "nothing to ship");
        return;
    }

    const std::string crop = defs_.crops.empty() ? std::string()
                           : defs_.crops[static_cast<std::size_t>(
                                 std::clamp(seed_, 0, static_cast<int>(defs_.crops.size()) - 1))].name;
    static constexpr Tool kTools[] = {Tool::Hoe, Tool::Water, Tool::Seed, Tool::Harvest};
    const ActionResult r = use_tool(world_, defs_, map_, kTools[static_cast<int>(tool_)],
                                    tx, ty, crop);
    if (r.ok && tool_ == Tab::Harvest) track("farm.harvest", "{\"day\":" + std::to_string(world_.day) + "}");
    say(r.message);
}

void FarmScene::update(double dt, const platform::InputState& in) {
    // Kept for render(), which draws the pad's pressed state and has no input of its
    // own. One frame stale by construction — the same borrow, in the other direction,
    // that the pointer makes from the camera.
    in_ = in;
    // Pumped before the ready check and before the dialogue returns early: a response
    // is not less finished because the player is reading a line, and a scene that
    // failed to load still has a connection to answer for.
    client_.update();
    if (!ready_) return;
    if (message_t_ > 0) message_t_ -= dt;
    if (step_cooldown_ > 0) step_cooldown_ -= dt;

    // Where the pointer is, built once. The dialogue reads it too — it has to, because
    // this branch RETURNS, and for five chapters that meant a phone player who opened
    // the dialogue box could not answer it, walk away or save. A hard lock, not a
    // missing convenience.
    const Pointer ptr{in.mouse_x, in.mouse_y,
                      in.down(platform::MouseButton::Left),
                      in.pressed(platform::MouseButton::Left)};

    // ---- dialogue owns the input while it is up ----
    if (talking_) {
        typed_ += dt * kTypeCharsPerSecond;
        const auto&      choices = talk_.choices();
        const TalkAction ta =
            read(talk_layout(screen_w_, screen_h_, static_cast<int>(choices.size())), ptr);
        const bool confirm = in.pressed(platform::Key::Z) ||
                             in.pressed(platform::Key::Space) ||
                             in.pressed(platform::Key::Enter);
        const auto finish_line = [&] { typed_ = static_cast<double>(talk_.text().size()); };

        if (!choices.empty()) {
            if (in.pressed(platform::Key::Up) && choice_ > 0) --choice_;
            if (in.pressed(platform::Key::Down) && choice_ + 1 < choices.size()) ++choice_;
            // A tap picks the option it landed on, so it MOVES the cursor before
            // choosing rather than choosing whatever the keyboard last highlighted:
            // otherwise the thing that lights up and the thing that happens disagree.
            if (ta.choice >= 0 && static_cast<std::size_t>(ta.choice) < choices.size())
                choice_ = static_cast<std::size_t>(ta.choice);
            if (confirm || ta.choice >= 0) {
                talk_.choose(choice_);
                choice_ = 0;
                typed_ = 0.0;
            } else if (ta.advance) {
                // A tap on the panel but off the options is not an answer. It finishes
                // the prompt, which is the one thing a reader wants from it.
                finish_line();
            }
        } else if (confirm || ta.advance) {
            // The first press finishes the line rather than skipping it — otherwise a
            // fast reader and a fast presser lose text they never saw.
            if (typed_ < static_cast<double>(talk_.text().size())) {
                finish_line();
            } else {
                talk_.advance();
                typed_ = 0.0;
            }
        }
        if (in.pressed(platform::Key::X) || in.pressed(platform::Key::Escape)) talking_ = false;
        if (talk_.finished()) talking_ = false;
        return;
    }

    // ---- clock ----
    if (advance(world_, dt)) sleep_now(/*collapsed*/ true);

    // ---- the on-screen pad ----
    // Read before movement so a thumb and the arrow keys reach the same code. It uses
    // the POINTER, because SDL synthesizes a mouse from a finger — one implementation
    // for a tap, a click and a trackpad, and no new event type at the platform seam.
    const Layout pad = layout(screen_w_, screen_h_, conflict_);
    const Action act = read(pad, ptr);

    // ---- movement ----
    int dx = 0, dy = 0;
    if (act.dx != 0 || act.dy != 0) { dx = act.dx; dy = act.dy; }
    else if (in.down(platform::Key::Left)  || in.down(platform::Key::A)) dx = -1;
    else if (in.down(platform::Key::Right) || in.down(platform::Key::D)) dx = 1;
    else if (in.down(platform::Key::Up)    || in.down(platform::Key::W)) dy = -1;
    else if (in.down(platform::Key::Down)  || in.down(platform::Key::S)) dy = 1;

    if (dx != 0 || dy != 0) {
        // Facing updates even when the step is blocked, so you can use a tool against
        // a wall you are pressed into rather than having to step away first.
        face_x_ = dx; face_y_ = dy;
        if (step_cooldown_ <= 0) {
            try_move(world_, map_, dx, dy);
            step_cooldown_ = kStepSeconds;
        }
    }

    // ---- the pointer picks the tile you are working on ----
    // The farm is the first thing in this project that consumes a mouse position at
    // all, and it is why the Play viewport's transform exists rather than being a
    // number nothing reads. The rule is the one the keyboard already had: you work on
    // an ADJACENT tile. The pointer chooses which of the four, and a click acts on it.
    //
    // The camera origin is the one from the last render — update() runs first and does
    // not know the viewport size. One frame of lag on a highlight is invisible; the
    // alternative is computing the camera twice and keeping the copies in agreement.
    // `act.consumed` is the veto. Without it a tap on the d-pad ALSO tills the tile
    // underneath it, which is the exact bug that makes an on-screen pad feel broken
    // rather than absent.
    if (in.mouse_x >= 0 && !act.consumed) {
        const tilemap::Vec2f o = cam_.origin();
        const int tx = static_cast<int>(std::floor((in.mouse_x + o.x) / kTile));
        const int ty = static_cast<int>(std::floor((in.mouse_y + o.y) / kTile));
        const int dx = tx - world_.px, dy = ty - world_.py;
        if (std::abs(dx) + std::abs(dy) == 1) {
            face_x_ = dx;
            face_y_ = dy;
            if (in.mouse_pressed[static_cast<int>(platform::MouseButton::Left)]) interact();
        }
    }

    // ---- tools, seeds, actions ----
    // Every line below reaches the same verb from a key and from a tap, and the two
    // are written on ONE line each on purpose: a second path that has to be kept in
    // step with the first is how a phone build ends up a version behind the keyboard.
    if (in.pressed(platform::Key::Num1)) tool_ = Tab::Hoe;
    if (in.pressed(platform::Key::Num2)) tool_ = Tab::Water;
    if (in.pressed(platform::Key::Num3)) tool_ = Tab::Seed;
    if (in.pressed(platform::Key::Num4)) tool_ = Tab::Harvest;
    // The hotbar was a picture of this state for five chapters. Now it is the control
    // for it, and it is the SAME four rectangles — read() answers from the boxes
    // render() draws, so the slot that lights up is the slot that was hit.
    if (act.tool >= 0) tool_ = static_cast<Tab>(act.tool);
    if ((in.pressed(platform::Key::Q) || act.seed) && !defs_.crops.empty())
        seed_ = (seed_ + 1) % static_cast<int>(defs_.crops.size());
    if (in.pressed(platform::Key::Z) || in.pressed(platform::Key::Space) || act.use) interact();
    if (in.pressed(platform::Key::F5) || act.save) save_game();
    // F9 has no button. Loading DISCARDS the day you are holding, and the farm has no
    // modal to ask twice with — so on a surface where the verbs are 44px squares under
    // a thumb, the destructive one stays behind a key you have to mean. Named in the
    // chapter as a ceiling, not left out and forgotten.
    if (in.pressed(platform::Key::F9)) load_game();
    // The conflict is OFFERED, never resolved for the player — the same rule the
    // Studio's autosave recovery follows, for the same reason: the machine cannot
    // know which evening's play matters. Reachable by thumb since chapter 126: the
    // chip has always named two keys, and a phone has neither, so a player who opened
    // the farm on a second device met a question they could not answer.
    if (conflict_ && (in.pressed(platform::Key::F6) || act.keep)) { conflict_ = false; push_save(); }
    if (conflict_ && (in.pressed(platform::Key::F7) || act.take)) adopt_cloud();

    update_npcs(world_, map_, schedules_);

    cam_.follow(tilemap::Vec2f{static_cast<float>(world_.px * kTile + kTile / 2),
                               static_cast<float>(world_.py * kTile + kTile / 2)},
                static_cast<float>(dt));
}

void FarmScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const int W = g.width(), H = g.height();
    screen_w_ = W;
    screen_h_ = H;
    g.clear(0xFF101418);
    g.set_font(ctx.font, th::sz_body);

    if (!ready_) {
        g.draw_text(th::space_lg, th::space_lg, problem_.c_str(), th::danger);
        return;
    }

    cam_.set_viewport(W, H);
    const tilemap::Vec2f o = cam_.origin();
    const int ox = -static_cast<int>(o.x), oy = -static_cast<int>(o.y);

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    cam_.visible_tiles(kTile, x0, y0, x1, y1);
    x0 = std::max(x0, 0); y0 = std::max(y0, 0);
    x1 = std::min(x1, map_.w - 1); y1 = std::min(y1, map_.h - 1);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int px = ox + x * kTile, py = oy + y * kTile;
            if (!draw_tile(g, "ground", map_.at("ground", x, y), x, y, px, py))
                g.fill_rect(px, py, kTile, kTile, ground_color(map_.at("ground", x, y)));

            if (const Soil* s = world_.at(x, y); s && s->tilled) {
                g.fill_rect(px, py, kTile, kTile, s->watered ? kWet : kTilled);
                if (s->crop >= 0) {
                    const CropDef& c = defs_.crops[static_cast<std::size_t>(s->crop)];
                    // The crop's size IS its stage: growth has to be visible from
                    // across the field or the whole watering loop is invisible.
                    const int span = std::max(1, c.stages - 1);
                    const int size = 3 + (kTile - 6) * s->stage / span;
                    const bool ripe = s->stage == c.stages - 1;
                    g.fill_rect(px + (kTile - size) / 2, py + (kTile - size) / 2, size, size,
                                ripe ? 0xFFE5B454 : 0xFF7FB069);
                }
            }
            if (const std::int32_t d = map_.at("decor", x, y); d != 0)
                if (!draw_tile(g, "decor", d, x, y, px, py))
                    g.fill_rect(px + 1, py + 1, kTile - 2, kTile - 2, decor_color(d));
        }
    }

    // The tile the tool would land on, so an action is never a guess.
    int tx = 0, ty = 0;
    facing(tx, ty);
    if (map_.in_bounds(tx, ty))
        g.draw_rect(ox + tx * kTile, oy + ty * kTile, kTile, kTile, 0xFFF4F1DE);

    for (const NpcState& n : world_.npcs)
        g.fill_circle(ox + n.x * kTile + kTile / 2, oy + n.y * kTile + kTile / 2, kTile / 2 - 2, kNpc);
    g.fill_circle(ox + world_.px * kTile + kTile / 2, oy + world_.py * kTile + kTile / 2,
                  kTile / 2 - 2, kPlayer);

    if (const int a = night_alpha(world_.minute); a > 0)
        g.fill_rect_blend(0, 0, W, H, gfx::rgba(6, 10, 40, a));

    // ---- HUD ----
    const int hud_h = 34;
    g.fill_rect_blend(0, 0, W, hud_h, 0xE0121420);
    g.set_font_size(th::sz_body);
    const std::string clock = "Day " + std::to_string(world_.day) + "   " + world_.time_text();
    g.draw_text(th::space_sm, 8, clock.c_str(), th::text);

    const int bar_x = 150, bar_w = 120;
    g.fill_rect(bar_x, 12, bar_w, 10, th::track);
    const int fill = bar_w * std::max(0, world_.energy) / kMaxEnergy;
    g.fill_rect(bar_x, 12, fill, 10, world_.energy > 25 ? th::success : th::danger);
    g.set_font_size(th::sz_caption);
    g.draw_text(bar_x, 1, "ENERGY", th::text_muted);

    g.set_font_size(th::sz_body);
    const std::string money = std::to_string(world_.gold) + "g";
    g.draw_text(bar_x + bar_w + th::space_lg, 8, money.c_str(), th::warn);

    // ---- cloud chip, top right --------------------------------------------------
    // One line, always present. "offline" is a state, not an error, and it is written
    // in the muted colour to say so: the farm is a whole game without a backend.
    {
        const gfx::Color tone = link_ == Link::Online   ? th::success
                              : link_ == Link::Connecting ? th::text_muted
                              : conflict_               ? th::warn
                                                        : th::text_dim;
        const std::string line = cloud_chip();
        const int cw = g.text_width(line.c_str());
        g.draw_text(W - cw - th::space_sm, 8, line.c_str(), conflict_ ? th::warn : tone);
        g.set_font_size(th::sz_caption);
        if (!event_name_.empty()) {
            const int ew = g.text_width(event_name_.c_str());
            g.draw_text(W - ew - th::space_sm, 1, event_name_.c_str(), th::accent);
        }
        g.set_font_size(th::sz_body);
    }

    // ---- hotbar -----------------------------------------------------------------
    // The tool used to be a word in the corner. Four slots say the same thing and one
    // more: what you are NOT holding, which is the half a word cannot show.
    //
    // Since chapter 126 the slots are also the CONTROL for it, and their geometry now
    // comes from controls.hpp with everything else on this screen. It used to be four
    // local constants here — which was fine while the hotbar was a picture, and became
    // the exact "drawn in one place, hit in another" bug the moment it answered a tap.
    static const char* kToolNames[] = {"Hoe", "Water", "Seed", "Harvest"};
    const Layout pad = layout(W, H, conflict_);

    // One line, once per process, to stderr — the same weight and the same reason as
    // the platform seam's "renderer 'opengl' (accelerated)". A tap that did nothing and
    // a tap that MISSED are different failures, and from outside the process nothing
    // else tells them apart. It is also how the browser touch check aims: at the button
    // the game says it drew, at the size the browser actually gave it, rather than at a
    // second copy of the rule (ch. 126).
    static bool announced = false;
    if (!announced) {
        announced = true;
        const auto box = [](const Box& b) {
            static char buf[64];
            std::snprintf(buf, sizeof buf, "%d,%d,%d,%d", b.x, b.y, b.w, b.h);
            return std::string(buf);
        };
        std::fprintf(stderr,
                     "farm: controls %dx%d up=%s down=%s left=%s right=%s use=%s seed=%s "
                     "save=%s tool0=%s\n",
                     W, H, box(pad.up).c_str(), box(pad.down).c_str(), box(pad.left).c_str(),
                     box(pad.right).c_str(), box(pad.use).c_str(), box(pad.seed).c_str(),
                     box(pad.save).c_str(), box(pad.tool[0]).c_str());
    }

    // Where the HUD strip begins, whether or not there is a hotbar in it. Anything
    // that sits above the hotbar has to keep sitting above it on the one screen too
    // small to have one, or a warning disappears exactly where it is hardest to spot.
    const int hud_top = pad.tool[0].empty() ? H - th::space_sm : pad.tool[0].y;
    // Two anchors, because the two things that float above the HUD are aligned
    // differently and only one of them meets the pad.
    //
    // The toast is CENTRED, so it clears the d-pad's column on its own and wants to sit
    // as close to the strip as it can.
    //
    // The config chip is LEFT-ALIGNED, which is straight into the d-pad — and a d-pad
    // button sitting on top of the one line an operator has to be able to read is the
    // same mistake as two buttons sharing a pixel, with the loss on the other side. It
    // was exactly that in the first cut of this chapter, and a screenshot is what said
    // so; neither a test nor the compiler had an opinion.
    const int toast_top = hud_top;
    int       chip_y    = pad.visible() ? pad.up.y : hud_top;
    if (!pad.tool[0].empty()) {
        for (int i = 0; i < 4; ++i) {
            const Box& b   = pad.tool[i];
            const bool on  = i == static_cast<int>(tool_);
            const bool hot = in_.mouse_x >= 0 && b.contains(in_.mouse_x, in_.mouse_y) &&
                             in_.down(platform::MouseButton::Left);
            g.fill_round_rect(b.x, b.y, b.w, b.h, th::radius_sm,
                              on ? 0xF02A3040 : (hot ? 0xD0242C3C : 0xC0161A24));
            if (on) g.draw_round_rect(b.x, b.y, b.w, b.h, th::radius_sm, th::accent);
            g.set_font_size(th::sz_caption);
            // Two caption rows, centred in whatever height the slot got: 24 on the
            // retro framebuffer, a thumb-sized 44 where it is tappable. Text pinned to
            // the top edge read fine at 24 and floats in the tall one.
            const bool sub = i == 2 && !defs_.crops.empty();
            const int  ty  = b.y + (b.h - (sub ? 20 : 10)) / 2;
            g.draw_text(b.x + 4, ty, std::to_string(i + 1).c_str(), th::text_dim);
            g.draw_text(b.x + 14, ty, kToolNames[i], on ? th::text : th::text_muted);
            // The seed slot carries what would be planted and how many are left — the
            // one number a player checks before walking to the far field.
            if (sub) {
                const CropDef& c = defs_.crops[static_cast<std::size_t>(
                    std::clamp(seed_, 0, static_cast<int>(defs_.crops.size()) - 1))];
                const auto have = world_.inventory.find(seed_item(c.name));
                const std::string sv = c.name + " x" +
                    std::to_string(have == world_.inventory.end() ? 0 : have->second);
                g.draw_text(b.x + 4, ty + 10, sv.c_str(), on ? th::accent : th::text_dim);
            }
        }
        const Box& last = pad.tool[3];
        const int  hx   = last.x + last.w + th::space_md;
        const int  hy   = last.y + (last.h - 20) / 2;
        g.set_font_size(th::sz_caption);
        g.draw_text(hx, hy,      "WASD move   Z use   Q seed", th::text_muted);
        g.draw_text(hx, hy + 10, "F5/F9 save/load", th::text_muted);
    }
    // An operator's typo in remote config has to be visible to whoever is playing
    // the build, or the only symptom is a price that quietly did not change.
    if (!config_problem_.empty()) {
        // On its own chip, not straight onto the field: warn-coloured text over a
        // green tile is a colour nobody can read, and this is the one line an
        // operator needs to be able to read from across the room.
        g.set_font_size(th::sz_caption);
        const int pw = g.text_width(config_problem_.c_str());
        const int ch = th::sz_caption + th::space_sm;
        chip_y -= ch + th::space_xs;
        g.fill_round_rect(th::space_sm - th::space_xs, chip_y, pw + th::space_sm,
                          ch, th::radius_sm, kProblemChip);
        g.draw_text(th::space_sm, chip_y + th::space_xs, config_problem_.c_str(), th::warn);
    }

    // ---- on-screen controls ------------------------------------------------------
    // Drawn from the SAME layout() the hit test uses. A control drawn in one place and
    // hit in another is a bug that is invisible in a screenshot: the button looks
    // right and does nothing.
    //
    // Always drawn, never auto-hidden. There is no reliable way to ask "is this a
    // touch device" — SDL hands us a mouse either way, by design — so the choice is
    // between guessing and showing. It is drawn faint enough to ignore, and a mouse
    // player gains a second way to walk rather than losing anything.
    // ...and NOT while the dialogue owns the input. update() returns before it reads
    // the pad, so every one of these buttons is dead for as long as the box is up —
    // and a control that is drawn and does nothing is the precise bug this whole file
    // is arranged to prevent, just arrived from the other direction.
    if (pad.visible() && !talking_) {
        const bool over = in_.mouse_x >= 0;
        const auto button = [&](const Box& b, const char* glyph) {
            // An empty box is a control that is not there this frame — `save` during a
            // conflict, `keep`/`take` outside one. Skipping it here is the same rule
            // read() follows by construction, said once.
            if (b.empty()) return;
            const bool hot = over && b.contains(in_.mouse_x, in_.mouse_y) &&
                             in_.down(platform::MouseButton::Left);
            g.fill_round_rect(b.x, b.y, b.w, b.h, th::radius_md,
                              hot ? 0xB03A4560 : 0x60202838);
            g.draw_round_rect(b.x, b.y, b.w, b.h, th::radius_md,
                              hot ? th::accent : 0x50FFFFFF);
            g.set_font_size(th::sz_body);
            const int tw = g.text_width(glyph);
            g.draw_text(b.x + (b.w - tw) / 2, b.y + (b.h - th::sz_body) / 2 + 1, glyph,
                        hot ? th::text : 0xC0FFFFFF);
        };
        button(pad.up, "^");
        button(pad.down, "v");
        button(pad.left, "<");
        button(pad.right, ">");
        button(pad.use, "Z");
        button(pad.seed, "Q");
        // Each button is labelled with the KEY it duplicates, like Z and Q above. That
        // is not decoration: it is what makes the two input paths teach each other —
        // and the cloud chip that has always read "F6 keep yours / F7 take cloud" is,
        // with no change at all, the legend for the two buttons that appear under it.
        button(pad.save, "F5");
        button(pad.keep, "F6");
        button(pad.take, "F7");
    }

    if (message_t_ > 0 && !message_.empty()) {
        g.set_font_size(th::sz_body);
        const int tw = g.text_width(message_.c_str());
        // Anchored ABOVE the hotbar rather than to a distance from the bottom edge.
        // It used to be `H - 60`, which sat clear of a 24 px strip and lands inside a
        // 44 px one — a number that was right about a layout it did not ask.
        const int my = toast_top - 26 - th::space_sm;
        g.fill_round_rect((W - tw) / 2 - th::space_md, my, tw + th::space_md * 2, 26,
                          th::radius_sm, 0xE01B1E28);
        g.draw_text((W - tw) / 2, my + 6, message_.c_str(), th::text);
    }

    // ---- dialogue box ----
    // Drawn from talk_layout(), the same rectangles the hit test reads. Before chapter
    // 126 the geometry was four expressions right here and nothing read it, which was
    // fine while the box was only a picture — and became a hard lock the moment a
    // player without a keyboard opened one.
    if (talking_) {
        const auto& choices = talk_.choices();
        const Talk  tk = talk_layout(W, H, static_cast<int>(choices.size()));
        if (tk.visible()) {
            const Box& b = tk.panel;
            g.fill_round_rect(b.x, b.y, b.w, b.h, th::radius_md, 0xF01B1B2B);
            g.draw_round_rect(b.x, b.y, b.w, b.h, th::radius_md, 0xFFF4F1DE);
            g.set_font_size(th::sz_caption);
            g.draw_text(b.x + th::space_md, b.y + th::space_sm, talk_.speaker().c_str(), th::warn);
            g.set_font_size(th::sz_body);
            const std::string full = talk_.text();
            const std::size_t shown =
                std::min(full.size(), static_cast<std::size_t>(std::max(0.0, typed_)));
            g.draw_text(b.x + th::space_md, b.y + th::space_sm + th::sz_caption + th::space_xs,
                        full.substr(0, shown).c_str(), th::text);
            for (int i = 0; i < tk.count; ++i) {
                const Box  r   = tk.choice(i);
                const bool sel = static_cast<std::size_t>(i) == choice_;
                const bool hot = in_.mouse_x >= 0 && r.contains(in_.mouse_x, in_.mouse_y);
                // The ROW lights up, because the row is what you press. An arrow in
                // front of the text says "a key moved a cursor here" and says nothing
                // at all about where to put a thumb.
                if (sel || hot)
                    g.fill_round_rect(r.x, r.y, r.w, r.h, th::radius_sm,
                                      sel ? 0x602A3040 : 0x30202838);
                g.draw_text(r.x + th::space_md, r.y + (r.h - th::sz_body) / 2,
                            ((sel ? "> " : "  ") + choices[static_cast<std::size_t>(i)].text).c_str(),
                            sel ? th::accent : th::text_dim);
            }
        }
    }
}

} // namespace farm
