// =============================================================================
//  games/farm/farm_scene.cpp
// =============================================================================
#include "games/farm/farm_scene.hpp"

#include <algorithm>
#include <cmath>

#include "engine/assets.hpp"
#include "engine/document/document.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"

namespace farm {

namespace th = ui::theme;

namespace {

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

FarmScene::FarmScene() { load(); }

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

    cam_.set_deadzone(24.0f, 16.0f);
    cam_.set_smoothing(0.90f);
    cam_.set_bounds(static_cast<float>(map_.w * kTile), static_cast<float>(map_.h * kTile));
    cam_.snap_to(tilemap::Vec2f{static_cast<float>(world_.px * kTile + kTile / 2),
                                static_cast<float>(world_.py * kTile + kTile / 2)});
    ready_ = true;
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

void FarmScene::save_game() {
    if (!doc::save(save_path(), doc::to_text(to_save(world_)))) { say("could not save"); return; }
    say("saved");
}

void FarmScene::load_game() {
    const std::string text = read_text(save_path());
    if (text.empty()) { say("no save yet"); return; }
    std::string why;
    const auto s = doc::load_save(text, "farm", kSaveVersion, migrations(), &why);
    if (!s) { say(why.empty() ? "save could not be read" : why, 5.0); return; }
    const auto w = from_save(*s);
    if (!w) { say("save could not be read", 5.0); return; }
    // The NPC list is not in the save: it is derived from the map, so a map that gains
    // an NPC does not need every old save migrated.
    world_ = *w;
    world_.npcs.clear();
    if (map_.entity("anna")) world_.npcs.push_back(NpcState{"anna", "", 0, 0});
    update_npcs(world_, map_, schedules_);
    cam_.snap_to(tilemap::Vec2f{static_cast<float>(world_.px * kTile + kTile / 2),
                                static_cast<float>(world_.py * kTile + kTile / 2)});
    say("loaded");
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
    say(r.message);
}

void FarmScene::update(double dt, const platform::InputState& in) {
    if (!ready_) return;
    if (message_t_ > 0) message_t_ -= dt;
    if (step_cooldown_ > 0) step_cooldown_ -= dt;

    // ---- dialogue owns the input while it is up ----
    if (talking_) {
        typed_ += dt * kTypeCharsPerSecond;
        const auto& choices = talk_.choices();
        if (!choices.empty()) {
            if (in.pressed(platform::Key::Up) && choice_ > 0) --choice_;
            if (in.pressed(platform::Key::Down) && choice_ + 1 < choices.size()) ++choice_;
            if (in.pressed(platform::Key::Z) || in.pressed(platform::Key::Space) ||
                in.pressed(platform::Key::Enter)) {
                talk_.choose(choice_);
                choice_ = 0;
                typed_ = 0.0;
            }
        } else if (in.pressed(platform::Key::Z) || in.pressed(platform::Key::Space) ||
                   in.pressed(platform::Key::Enter)) {
            // The first press finishes the line rather than skipping it — otherwise a
            // fast reader and a fast presser lose text they never saw.
            if (typed_ < static_cast<double>(talk_.text().size())) {
                typed_ = static_cast<double>(talk_.text().size());
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

    // ---- movement ----
    int dx = 0, dy = 0;
    if (in.down(platform::Key::Left)  || in.down(platform::Key::A)) dx = -1;
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

    // ---- tools, seeds, actions ----
    if (in.pressed(platform::Key::Num1)) tool_ = Tab::Hoe;
    if (in.pressed(platform::Key::Num2)) tool_ = Tab::Water;
    if (in.pressed(platform::Key::Num3)) tool_ = Tab::Seed;
    if (in.pressed(platform::Key::Num4)) tool_ = Tab::Harvest;
    if (in.pressed(platform::Key::Q) && !defs_.crops.empty())
        seed_ = (seed_ + 1) % static_cast<int>(defs_.crops.size());
    if (in.pressed(platform::Key::Z) || in.pressed(platform::Key::Space)) interact();
    if (in.pressed(platform::Key::F5)) save_game();
    if (in.pressed(platform::Key::F9)) load_game();

    update_npcs(world_, map_, schedules_);

    cam_.follow(tilemap::Vec2f{static_cast<float>(world_.px * kTile + kTile / 2),
                               static_cast<float>(world_.py * kTile + kTile / 2)},
                static_cast<float>(dt));
}

void FarmScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const int W = g.width(), H = g.height();
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

    static const char* kToolNames[] = {"Hoe", "Water", "Seed", "Harvest"};
    std::string right = kToolNames[static_cast<int>(tool_)];
    if (tool_ == Tab::Seed && !defs_.crops.empty()) {
        const CropDef& c = defs_.crops[static_cast<std::size_t>(
            std::clamp(seed_, 0, static_cast<int>(defs_.crops.size()) - 1))];
        const auto have = world_.inventory.find(seed_item(c.name));
        right += ": " + c.name + " x" +
                 std::to_string(have == world_.inventory.end() ? 0 : have->second);
    }
    g.draw_text(W - g.text_width(right.c_str()) - th::space_sm, 8, right.c_str(), th::accent);

    g.set_font_size(th::sz_caption);
    const char* keys = "WASD move   Z use   1-4 tool   Q seed   F5/F9 save/load";
    g.draw_text(th::space_sm, H - th::sz_caption - th::space_xs, keys, th::text_muted);

    if (message_t_ > 0 && !message_.empty()) {
        g.set_font_size(th::sz_body);
        const int tw = g.text_width(message_.c_str());
        g.fill_round_rect((W - tw) / 2 - th::space_md, H - 60, tw + th::space_md * 2, 26,
                          th::radius_sm, 0xE01B1E28);
        g.draw_text((W - tw) / 2, H - 54, message_.c_str(), th::text);
    }

    // ---- dialogue box ----
    if (talking_) {
        const auto& choices = talk_.choices();
        const int box_h = 70 + static_cast<int>(choices.size()) * 18;
        const int by = H - box_h - th::space_md;
        g.fill_round_rect(th::space_md, by, W - th::space_md * 2, box_h, th::radius_md, 0xF01B1B2B);
        g.draw_round_rect(th::space_md, by, W - th::space_md * 2, box_h, th::radius_md, 0xFFF4F1DE);
        g.set_font_size(th::sz_caption);
        g.draw_text(th::space_md + th::space_md, by + th::space_sm, talk_.speaker().c_str(),
                    th::warn);
        g.set_font_size(th::sz_body);
        const std::string full = talk_.text();
        const std::size_t shown = std::min(full.size(), static_cast<std::size_t>(std::max(0.0, typed_)));
        g.draw_text(th::space_md + th::space_md, by + th::space_sm + th::sz_caption + th::space_xs,
                    full.substr(0, shown).c_str(), th::text);
        int cy = by + 60;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            const bool sel = i == choice_;
            g.draw_text(th::space_md + th::space_xl, cy,
                        ((sel ? "> " : "  ") + choices[i].text).c_str(),
                        sel ? th::accent : th::text_dim);
            cy += 18;
        }
    }
}

} // namespace farm
