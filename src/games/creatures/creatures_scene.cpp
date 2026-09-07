// =============================================================================
//  games/creatures/creatures_scene.cpp
// =============================================================================
#include "games/creatures/creatures_scene.hpp"

#include <algorithm>
#include <cstdio>

#include "engine/assets.hpp"
#include "engine/image.hpp"
#include "engine/text/font.hpp"
#include "engine/ui/theme.hpp"

namespace th = ui::theme;

namespace creature {

namespace {

std::string read_text(const std::string& path) {
    const auto bytes = assets::load_file(path);
    return bytes ? std::string(bytes->begin(), bytes->end()) : std::string();
}

constexpr double kStepSeconds = 0.11;      // one tile, then a pause: a grid game

const char* kMenu[4]  = {"Fight", "Ball", "Party", "Run"};

// Which ink is readable ON a fill. Picking one constant and hoping is how the first
// version of the battle menu came out near-black text on a near-black button — every
// label present, every label unreadable, and no assertion that could tell.
gfx::Color ink_on(gfx::Color fill) {
    const int lum = (gfx::r_of(fill) * 299 + gfx::g_of(fill) * 587 + gfx::b_of(fill) * 114) / 1000;
    return lum > 140 ? th::bg : th::text;
}

gfx::Color type_colour(int t) {
    static const gfx::Color kByType[6] = {
        gfx::rgb(0xb9, 0x90, 0x6a),   // normal
        gfx::rgb(0xe0, 0x5a, 0x2a),   // fire
        gfx::rgb(0x3a, 0x7b, 0xd5),   // water
        gfx::rgb(0x4f, 0x9d, 0x3a),   // grass
        gfx::rgb(0xe0, 0xc0, 0x20),   // electric
        gfx::rgb(0x8a, 0x8f, 0x99),   // rock
    };
    return kByType[t < 0 || t > 5 ? 0 : t];
}

} // namespace

CreaturesScene::CreaturesScene() { load(); }

void CreaturesScene::load() {
    problem_.clear();
    // The list of files lives in defs.hpp, not here. It was written out in three
    // places until chapter 138, and the headless verifier would have been a fourth —
    // four copies of "which files are the rules", one of which a person eventually
    // forgets to update.
    if (!load_dex(dex_, [](const char* f, std::string& out) {
            const auto bytes = assets::load_file(f);
            if (!bytes) return false;
            out.assign(bytes->begin(), bytes->end());
            return true;
        }, &problem_))
        return;
    if (const auto bad = validate(dex_); !bad.empty()) { problem_ = bad.front(); return; }

    const auto m = tilemap::load(read_text("maps/creature_route.map2"));
    if (!m) { problem_ = "maps/creature_route.map2 will not load"; return; }
    map_ = *m;

    if (auto th = tilemap::parse_theme(read_text("creatures/theme.def"))) {
        theme_ = *th;
        for (const auto& [name, sh] : theme_.sheets) {
            tilemap::Tileset& into = tiles_[name];
            if (const auto img = gfx::load_image(sh.path)) into = tilemap::Tileset::cut(*img, sh.tile);
        }
    }

    // Where the game begins is a fact about the MAP, not a constant in here: the
    // route declares `entity home`, so moving the cabin in an editor moves the spawn.
    int hx = 4, hy = 6;
    for (const tilemap::Entity& e : map_.entities)
        if (e.name == "home") { hx = e.x; hy = e.y; }

    world_ = new_game(dex_, 1, hx, hy, 0x5EEDu);

    cam_.set_viewport(static_cast<float>(fb_w_), static_cast<float>(fb_h_));
    cam_.set_deadzone(32.0f, 24.0f);
    cam_.set_smoothing(0.90f);
    cam_.set_bounds(static_cast<float>(map_.w * kTile), static_cast<float>(map_.h * kTile));
    cam_.snap_to(tilemap::Vec2f{static_cast<float>(world_.px * kTile + kTile / 2),
                                static_cast<float>(world_.py * kTile + kTile / 2)});
    ready_ = true;
    load_game();
}

Mode CreaturesScene::mode() const {
    switch (world_.phase) {
        case Phase::Overworld: return Mode::Overworld;
        case Phase::Battle:    return in_moves_ ? Mode::Moves : (in_party_ ? Mode::Party : Mode::Menu);
        default:               return Mode::Ack;
    }
}

const tilemap::Tileset& CreaturesScene::sheet_of(const std::string& name) const {
    static const tilemap::Tileset kNone;
    const auto it = tiles_.find(name);
    return it == tiles_.end() ? kNone : it->second;
}

const gfx::Image* CreaturesScene::creature_image(int species) const {
    if (const auto it = sprites_.find(species); it != sprites_.end())
        return it->second.w > 0 ? &it->second : nullptr;
    const SpeciesDef* s = dex_.species_by_id(species);
    gfx::Image& into = sprites_[species];
    if (s) if (const auto img = gfx::load_image(s->sprite)) into = *img;
    return into.w > 0 ? &into : nullptr;
}

bool CreaturesScene::draw_tile(gfx::Renderer2D& g, const char* layer, std::int32_t id,
                               int x, int y, int px, int py) const {
    const tilemap::Theme::Art* a = theme_.find(layer, static_cast<int>(id));
    if (!a) return false;
    const int index = a->index + tilemap::rule_piece(map_, layer, x, y);
    const gfx::Sprite s = sheet_of(a->sheet).sprite(static_cast<std::size_t>(index));
    if (s.w == 0) return false;
    g.blit(s, px, py);
    return true;
}

void CreaturesScene::say(std::string msg, double seconds) {
    message_ = std::move(msg);
    message_t_ = seconds;
}

std::string save_path()  { return "saves/creatures/slot1.sav"; }
std::string tape_path()  { return "saves/creatures/last_battle.crep"; }

bool CreaturesScene::save_game() {
    const std::string text = to_text(world_);
    return assets::write_file(save_path(),
                              std::vector<std::uint8_t>(text.begin(), text.end()));
}

// Written the moment a battle ends, without being asked. A recording that a player
// has to remember to make is a recording nobody has when it matters — and this file
// is the ONLY thing this game produces that a different machine can check. Failure
// is silent on purpose: a full disk must not eat the fight you just won.
void CreaturesScene::write_tape() {
    std::string why;
    const std::string text = write_replay(world_.tape, &why);
    if (text.empty()) return;
    assets::write_file(tape_path(), std::vector<std::uint8_t>(text.begin(), text.end()));
}

bool CreaturesScene::load_game() {
    const std::string text = read_text(save_path());
    if (text.empty()) return false;
    World w;
    if (!from_text(dex_, text, w)) return false;
    world_ = w;
    cam_.snap_to(tilemap::Vec2f{static_cast<float>(world_.px * kTile + kTile / 2),
                                static_cast<float>(world_.py * kTile + kTile / 2)});
    return true;
}

// ---- turning a chosen cell into a turn -----------------------------------------

void CreaturesScene::narrate() {
    // The events are structured on purpose (no strings in the core), so the sentence
    // is made HERE. Only the last interesting one is kept: a turn produces up to a
    // dozen events and a log that showed all of them would scroll past unread.
    for (const Event& e : world_.log) {
        switch (e.kind) {
            case Event::Kind::Used: {
                const MoveDef* mv = dex_.move(e.a);
                say(std::string(e.side == 0 ? "You use " : "It uses ") + (mv ? mv->name : "?"));
                break;
            }
            case Event::Kind::Missed:      say(e.side == 0 ? "You missed" : "It missed"); break;
            case Event::Kind::Fainted:     say(e.side == 0 ? "Yours fainted" : "It fainted"); break;
            case Event::Kind::Inflicted:   say("Status!"); break;
            case Event::Kind::Immobilised: say(e.side == 0 ? "You can't move" : "It can't move"); break;
            case Event::Kind::NoPP:        say("No PP left"); break;
            default: break;
        }
    }
}

void CreaturesScene::choose_cell(int cell) {
    if (cell < 0) return;
    if (world_.phase != Phase::Battle) return;

    if (in_moves_) {
        if (cell > 3) return;
        const Creature& me = world_.battle.side[0].now();
        if (me.moves[cell].move < 0) { say("No move there"); return; }
        in_moves_ = false;
        battle_turn(dex_, world_, Action{Action::Kind::Move, cell});
        narrate();
        return;
    }
    if (in_party_) {
        if (cell >= kPartySize) return;
        const Party& p = world_.battle.side[0];
        if (cell == p.active)            { say("Already out"); return; }
        if (!p.member[cell].alive())     { say("It has fainted"); return; }
        in_party_ = false;
        battle_turn(dex_, world_, Action{Action::Kind::Switch, cell});
        narrate();
        return;
    }
    switch (cell) {
        case 0: in_moves_ = true; break;
        case 1:
            if (world_.balls <= 0) { say("No balls left"); break; }
            say(throw_ball(dex_, world_) ? "Caught it!" : "It broke free");
            break;
        case 2: in_party_ = true; break;
        case 3: battle_turn(dex_, world_, Action{Action::Kind::Run, 0}); say("Got away"); break;
        default: break;
    }
}

// ---- update ---------------------------------------------------------------------

// One place asks "did a fight just end", because the fight can end in four
// different branches below (a knockout, a catch, a run, a blackout) and a write
// placed in three of them is a recording that is missing exactly one outcome.
void CreaturesScene::update(double dt, const platform::InputState& input) {
    if (!ready_) return;
    const bool was_fighting = world_.phase == Phase::Battle;
    update_world(dt, input);
    if (was_fighting && world_.phase != Phase::Battle) write_tape();
}

void CreaturesScene::update_world(double dt, const platform::InputState& input) {
    if (message_t_ > 0) message_t_ -= dt;
    if (step_cool_ > 0) step_cool_ -= dt;

    Pointer p;
    p.x = input.mouse_x;
    p.y = input.mouse_y;
    p.down    = input.mouse_down[static_cast<int>(platform::MouseButton::Left)];
    p.pressed = input.mouse_pressed[static_cast<int>(platform::MouseButton::Left)];
    const Mode m = mode();
    const Press press = read(layout(fb_w_, fb_h_, m), m, p);

    const auto key = [&](platform::Key k) {
        return input.key_down[static_cast<int>(k)];
    };
    const auto hit = [&](platform::Key k) {
        return input.key_pressed[static_cast<int>(k)];
    };

    if (m == Mode::Overworld) {
        int dx = press.dx, dy = press.dy;
        if (key(platform::Key::A) || key(platform::Key::Left))  dx = -1;
        if (key(platform::Key::D) || key(platform::Key::Right)) dx =  1;
        if (key(platform::Key::W) || key(platform::Key::Up))    dy = -1;
        if (key(platform::Key::S) || key(platform::Key::Down))  dy =  1;
        if (dx != 0 && dy != 0) dy = 0;                 // one axis at a time
        if ((dx != 0 || dy != 0) && step_cool_ <= 0) {
            const WalkResult r = walk(dex_, world_, map_, dx, dy);
            step_cool_ = kStepSeconds;
            if (r.encounter) {
                const SpeciesDef* s = dex_.species_by_id(world_.wild_species);
                say(std::string("A wild ") + (s ? s->name : "?") + " appeared!", 3.0);
                in_moves_ = in_party_ = false;
            }
        }
        if (press.save || hit(platform::Key::F5))
            say(save_game() ? "Saved" : "Could not save");
        if (press.act || hit(platform::Key::Z)) {
            // Home is the only interaction on the route, and it is a heal — which is
            // also what a blackout does for you, so the two paths share a function.
            if (world_.px == world_.home_x && world_.py == world_.home_y) {
                const Phase keep = world_.phase;
                world_.phase = Phase::Blackout;
                end_battle(dex_, world_);
                world_.phase = keep == Phase::Overworld ? Phase::Overworld : keep;
                say("Your party is rested");
            } else {
                say("Nothing here");
            }
        }
        return;
    }

    if (m == Mode::Ack) {
        if (press.ack || hit(platform::Key::Z) || hit(platform::Key::Enter) ||
            hit(platform::Key::Space)) {
            end_battle(dex_, world_);
            in_moves_ = in_party_ = false;
        }
        return;
    }

    // ---- a battle menu ----
    if (press.back || hit(platform::Key::X) || hit(platform::Key::Escape)) {
        in_moves_ = in_party_ = false;
        return;
    }
    int cell = press.cell;
    const platform::Key digits[6] = {platform::Key::Num1, platform::Key::Num2,
                                     platform::Key::Num3, platform::Key::Num4,
                                     platform::Key::Num5, platform::Key::Num6};
    for (int i = 0; i < 6; ++i) if (hit(digits[i])) cell = i;
    if (cell >= 0) choose_cell(cell);
}

// ---- render ---------------------------------------------------------------------

void CreaturesScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    fb_w_ = g.width();
    fb_h_ = g.height();
    if (ctx.font) g.set_font(ctx.font, th::sz_body);

    if (!ready_) {
        g.clear(th::bg);
        g.draw_text(16, 24, ("creatures: " + problem_).c_str(), th::danger);
        return;
    }
    cam_.set_viewport(static_cast<float>(fb_w_), static_cast<float>(fb_h_));
    if (mode() == Mode::Overworld) render_overworld(ctx);
    else                           render_battle(ctx);
}

void CreaturesScene::render_overworld(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    cam_.follow(tilemap::Vec2f{static_cast<float>(world_.px * kTile + kTile / 2),
                               static_cast<float>(world_.py * kTile + kTile / 2)},
                static_cast<float>(ctx.dt));
    const tilemap::Vec2f o = cam_.origin();
    org_x_ = -static_cast<int>(o.x);
    org_y_ = -static_cast<int>(o.y);

    g.clear(gfx::rgb(0x2c, 0x38, 0x2c));
    const int x0 = std::max(0, static_cast<int>(o.x) / kTile);
    const int y0 = std::max(0, static_cast<int>(o.y) / kTile);
    const int x1 = std::min(map_.w - 1, (static_cast<int>(o.x) + fb_w_) / kTile);
    const int y1 = std::min(map_.h - 1, (static_cast<int>(o.y) + fb_h_) / kTile);

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const int px = org_x_ + x * kTile, py = org_y_ + y * kTile;
            const std::int32_t gid = map_.at("ground", x, y);
            if (!draw_tile(g, "ground", gid, x, y, px, py)) {
                const gfx::Color flat = gid == kWater ? gfx::rgb(0x35, 0x6f, 0xa8)
                                      : gid == kPath  ? gfx::rgb(0x9a, 0x84, 0x60)
                                      : gid == kLongGrass ? gfx::rgb(0x3f, 0x7d, 0x3a)
                                                          : gfx::rgb(0x5a, 0x9a, 0x4f);
                g.fill_rect(px, py, kTile, kTile, flat);
            }
            if (const std::int32_t did = map_.at("decor", x, y); did != 0)
                draw_tile(g, "decor", did, x, y, px, py);
        }
    }

    const int ppx = org_x_ + world_.px * kTile, ppy = org_y_ + world_.py * kTile;
    if (const tilemap::Theme::Art* a = theme_.actor("player")) {
        const gfx::Sprite s = sheet_of(a->sheet).sprite(static_cast<std::size_t>(a->index));
        if (s.w > 0) g.blit(s, ppx, ppy);
        else         g.fill_rect(ppx + 3, ppy + 2, 10, 12, th::accent);
    }

    // ---- HUD ----
    const Creature& lead = world_.party.now();
    const SpeciesDef* s = dex_.species_by_id(lead.species);
    char line[128];
    std::snprintf(line, sizeof line, "%s  Lv%d  %d/%d   balls %d   won %d   caught %d",
                  s ? s->name.c_str() : "-", lead.level, lead.hp, lead.max_hp,
                  world_.balls, world_.wins, world_.caught);
    g.fill_rect(0, 0, fb_w_, 22, gfx::rgba(0, 0, 0, 150));
    g.draw_text(8, 6, line, th::text);
    if (message_t_ > 0) {
        g.fill_rect(0, fb_h_ - 24, fb_w_, 24, gfx::rgba(0, 0, 0, 170));
        g.draw_text(8, fb_h_ - 19, message_.c_str(), th::text);
    }
    render_controls(g);
}

void CreaturesScene::render_battle(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const Layout l = layout(fb_w_, fb_h_, mode());

    g.clear(gfx::rgb(0x1b, 0x22, 0x2c));
    g.fill_rect(0, 0, fb_w_, fb_h_ / 2, gfx::rgb(0x2e, 0x3d, 0x4e));
    g.fill_rect(0, fb_h_ / 2, fb_w_, fb_h_ / 2, gfx::rgb(0x24, 0x33, 0x26));

    const auto bar = [&](int x, int y, int w, const Creature& c, const char* who) {
        const SpeciesDef* s = dex_.species_by_id(c.species);
        char t[96];
        std::snprintf(t, sizeof t, "%s %s  Lv%d", who, s ? s->name.c_str() : "-", c.level);
        g.draw_text(x, y, t, th::text);
        const int bw = w, bh = 8;
        g.fill_rect(x, y + 18, bw, bh, gfx::rgb(0x14, 0x18, 0x1e));
        const int fill = c.max_hp > 0 ? bw * c.hp / c.max_hp : 0;
        const gfx::Color hue = c.hp * 4 <= c.max_hp ? th::danger
                             : c.hp * 2 <= c.max_hp ? th::warn : th::success;
        g.fill_rect(x, y + 18, std::max(0, fill), bh, hue);
        if (c.status != Status::None) {
            static const char* kTag[4] = {"", "BRN", "PAR", "SLP"};
            g.draw_text(x + bw + 6, y + 14, kTag[static_cast<int>(c.status)], th::warn);
        }
    };

    const Creature& mine = world_.battle.side[0].now();
    const Creature& them = world_.battle.side[1].now();

    // Both sprite rects come from the LAYOUT, like every other rectangle on this
    // screen. A creature placed by its own arithmetic is what `Back` was drawn on top
    // of, and it is what a test cannot check without re-deriving.
    const auto creature = [&](const Box& b, const Creature& c) {
        if (const gfx::Image* img = creature_image(c.species)) {
            g.blit_scaled(gfx::Sprite{img->pixels.data(), img->w, img->h}, b.x, b.y, b.w, b.h);
        } else {
            const SpeciesDef* s = dex_.species_by_id(c.species);
            g.fill_rect(b.x, b.y, b.w, b.h, type_colour(s ? s->type : 0));
        }
    };
    creature(l.theirs, them);
    creature(l.mine, mine);
    bar(24, 26, 150, them, "Wild");
    bar(fb_w_ / 2 + 24, l.panel.y - 54, 150, mine, "Your");

    // ---- the panel ----
    g.fill_rect(l.panel.x, l.panel.y, l.panel.w, l.panel.h, gfx::rgba(0x10, 0x14, 0x1a, 235));
    g.draw_line(l.panel.x, l.panel.y, l.panel.x + l.panel.w, l.panel.y, th::border);
    if (!message_.empty()) g.draw_text(l.log.x, l.log.y + 4, message_.c_str(), th::text);

    char sub[96];
    switch (mode()) {
        case Mode::Ack:
            std::snprintf(sub, sizeof sub, "%s",
                world_.phase == Phase::Won      ? "It fainted."
              : world_.phase == Phase::Caught   ? "It joined your party."
              : world_.phase == Phase::Blackout ? "You blacked out."
                                                : "You got away.");
            g.draw_text(l.log.x, l.log.y + 26, sub, th::text_dim);
            g.fill_rect(l.ack.x, l.ack.y, l.ack.w, l.ack.h, th::accent);
            g.draw_text(l.ack.x + 28, l.ack.y + 14, "Continue", ink_on(th::accent));
            render_controls(g);
            return;
        default: break;
    }

    for (int i = 0; i < 6; ++i) {
        const Box& b = l.cell[i];
        if (b.empty()) continue;
        std::string label = "-";
        gfx::Color tint = th::ctrl;
        if (mode() == Mode::Menu && i < 4) {
            label = kMenu[i];
            if (i == 1) label += " x" + std::to_string(world_.balls);
        } else if (mode() == Mode::Moves && i < kMoveSlots) {
            const MoveSlot& ms = mine.moves[i];
            if (const MoveDef* mv = dex_.move(ms.move)) {
                label = mv->name + "  " + std::to_string(ms.pp);
                tint  = type_colour(mv->type);
            }
        } else if (mode() == Mode::Party && i < kPartySize) {
            const Creature& c = world_.battle.side[0].member[i];
            if (c.species != 0) {
                const SpeciesDef* s = dex_.species_by_id(c.species);
                label = (s ? s->name : "?") + "  " + std::to_string(c.hp);
                tint  = c.alive() ? type_colour(s ? s->type : 0) : th::ctrl;
            }
        }
        g.fill_rect(b.x, b.y, b.w, b.h, tint);
        g.draw_rect(b.x, b.y, b.w, b.h, th::border);
        g.draw_text(b.x + 8, b.y + b.h / 2 - 6, label.c_str(), ink_on(tint));
    }
    if (!l.back.empty()) {
        g.fill_rect(l.back.x, l.back.y, l.back.w, l.back.h, gfx::rgba(0x10, 0x14, 0x1a, 220));
        g.draw_rect(l.back.x, l.back.y, l.back.w, l.back.h, th::border);
        g.draw_text(l.back.x + 20, l.back.y + l.back.h / 2 - 6, "Back", th::text);
    }
    render_controls(g);
}

void CreaturesScene::render_controls(gfx::Renderer2D& g) const {
    const Layout l = layout(fb_w_, fb_h_, mode());

    // One line, once per process, to stderr — the same line the farm prints and for
    // the same reason (chapter 126): a tap that did nothing and a tap that MISSED are
    // different failures, and from outside the process nothing else tells them apart.
    // It is also how the browser check aims — at the button the game says it drew, at
    // the size the browser actually gave it, rather than at a second copy of the rule.
    const auto box = [](const Box& b) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d,%d,%d,%d", b.x, b.y, b.w, b.h);
        return std::string(buf);
    };
    static bool announced = false;
    if (!announced && l.pad_visible()) {
        announced = true;
        std::fprintf(stderr,
                     "creatures: controls %dx%d up=%s down=%s left=%s right=%s act=%s save=%s\n",
                     fb_w_, fb_h_, box(l.up).c_str(), box(l.down).c_str(),
                     box(l.left).c_str(), box(l.right).c_str(),
                     box(l.act).c_str(), box(l.save).c_str());
    }

    // A SECOND line, the first time a battle is on screen. The farm needed one line
    // because everything it can do is on one screen; this game has two, and the
    // browser check cannot finish a fight it cannot aim at. Printed from here, at the
    // moment the screen exists, for the same reason as the first: the numbers a
    // checker uses have to be the numbers the renderer used.
    static bool announced_battle = false;
    if (!announced_battle && mode() != Mode::Overworld && !l.panel.empty()) {
        announced_battle = true;
        std::fprintf(stderr,
                     "creatures: battle %dx%d fight=%s ball=%s party=%s run=%s ack=%s back=%s\n",
                     fb_w_, fb_h_, box(l.cell[0]).c_str(), box(l.cell[1]).c_str(),
                     box(l.cell[2]).c_str(), box(l.cell[3]).c_str(),
                     box(layout(fb_w_, fb_h_, Mode::Ack).ack).c_str(), box(l.back).c_str());
    }

    if (!l.pad_visible()) return;
    const auto btn = [&](const Box& b, const char* label) {
        if (b.empty()) return;
        g.fill_rect(b.x, b.y, b.w, b.h, gfx::rgba(0x10, 0x14, 0x1a, 170));
        g.draw_rect(b.x, b.y, b.w, b.h, gfx::rgba(0xff, 0xff, 0xff, 90));
        g.draw_text(b.x + b.w / 2 - 8, b.y + b.h / 2 - 6, label, th::text);
    };
    btn(l.up, "^"); btn(l.down, "v"); btn(l.left, "<"); btn(l.right, ">");
    btn(l.act, "Z"); btn(l.save, "S");
}

} // namespace creature
