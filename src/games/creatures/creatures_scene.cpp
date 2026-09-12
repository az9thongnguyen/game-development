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
#include "engine/ui/touch_input.hpp"

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

std::string display_name(std::string name) {
    if (!name.empty() && name[0] >= 'a' && name[0] <= 'z')
        name[0] = static_cast<char>(name[0] - 'a' + 'A');
    return name;
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
            tilemap::AnimatedTileset& into = tiles_[name];
            if (const auto img = gfx::load_image(sh.path))
                into = tilemap::AnimatedTileset::cut(*img, sh.tile);
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
    // A rated session owns the screen while it exists. Checked FIRST, and derived from
    // the client's own state rather than from a flag this file keeps beside it: a
    // second copy of "what is happening" is how the menu and the renderer end up
    // disagreeing about which battle they are showing.
    if (online_) {
        switch (online_->state()) {
            case PvpClient::State::Playing:
                return in_moves_ ? Mode::Moves : (in_party_ ? Mode::Party : Mode::Menu);
            case PvpClient::State::Done:
            case PvpClient::State::Failed:
                return Mode::Ack;
            default:
                return Mode::Online;
        }
    }
    switch (world_.phase) {
        case Phase::Overworld: return Mode::Overworld;
        case Phase::Battle:    return in_moves_ ? Mode::Moves : (in_party_ ? Mode::Party : Mode::Menu);
        default:               return Mode::Ack;
    }
}

// Keyed on the PROTOCOL having started, not on the session still being in `Playing`.
// It was the latter for about an hour, and the result screen — which by definition is
// reached after the match is over — then drew `world_.battle`: the last wild fight, or
// nothing at all. A "what is on screen" question answered by a state that has already
// moved on is answered for every frame except the one that matters.
bool CreaturesScene::net_battle_live() const {
    return online_ && online_->net().phase() != NetPhase::Idle;
}

const Battle& CreaturesScene::shown_battle() const {
    return net_battle_live() ? online_->net().battle() : world_.battle;
}

int CreaturesScene::my_side() const {
    return net_battle_live() ? online_->net().side() : 0;
}

gbaas::Config CreaturesScene::default_online_config() {
    // `default_base_url()` is the SDK's own answer and is `#ifdef`'d once, in one place:
    // relative on the web (the page and the API are one origin) and 127.0.0.1:8080
    // natively. Writing `""` here instead — which this function did for about an hour —
    // means a desktop build has no server at all and the button fails instantly.
    // The api key is the project `--pvp` uses, so a match found from this screen and one
    // found from a terminal are the same ladder.
    return {gbaas::default_base_url(), "pk_demo_creatures"};
}

bool CreaturesScene::start_online(gbaas::Config cfg,
                                  std::unique_ptr<gbaas::ITransport> transport) {
    if (online_) return false;                       // one session at a time
    if (world_.phase != Phase::Overworld) return false;   // one fight at a time
    // The party goes over the wire as `species:level` (netbattle rebuilds the creatures
    // from the dex on both sides), so what is sent is a fresh, unhurt copy of the team
    // — a rated match does not start with the damage a wild one left behind.
    //
    // Built by `make_party`, NOT by filling a Party in place. The first version of this
    // did the latter, set `member[]` and `active`, and forgot `count` — which is the
    // field `write_party` iterates, so the wire carried an empty party and the peer
    // refused the match. A struct with a constructor function has that function for a
    // reason: it is the only place that knows all of its fields.
    std::vector<std::pair<int, int>> spec;
    for (int i = 0; i < kPartySize; ++i) {
        const Creature& c = world_.party.member[i];
        if (c.species != 0) spec.emplace_back(c.species, c.level);
    }
    if (spec.empty()) { say("No party to bring"); return false; }
    const Party mine = make_party(dex_, spec);

    online_ = std::make_unique<PvpClient>(dex_, std::move(cfg), std::string(kLadderBoard),
                                          std::move(transport));
    // The screen picks the action. This one line is the whole difference between the
    // headless client and this one, which is what pvp.hpp predicted it would be.
    online_->set_auto_play(false);
    online_->start(mine);
    online_ack_ = false;
    online_note_.clear();
    in_moves_ = in_party_ = false;
    return true;
}

void CreaturesScene::cancel_online() {
    if (!online_) return;
    online_->cancel();
    online_.reset();
    online_note_.clear();
    in_moves_ = in_party_ = false;
}

const tilemap::AnimatedTileset& CreaturesScene::sheet_of(const std::string& name) const {
    static const tilemap::AnimatedTileset kNone;
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
    const bool net = online_ && online_->state() == PvpClient::State::Playing;
    if (!net && world_.phase != Phase::Battle) return;

    // In a rated match the peer has to agree before anything resolves, so a tap while
    // the last turn is still in flight must do NOTHING — not queue, not resolve
    // locally. `waiting_for_action()` is the one place that decides, and it says no
    // between turns as well as when it is the opponent's move.
    if (net && !online_->waiting_for_action()) {
        if (cell == 3 && !in_moves_ && !in_party_) return;   // Run: handled below
        say("Waiting for your opponent");
        return;
    }

    const Party& mine_side = shown_battle().side[my_side()];

    if (in_moves_) {
        if (cell > 3) return;
        const Creature& me = mine_side.now();
        if (me.moves[cell].move < 0) { say("No move there"); return; }
        in_moves_ = false;
        if (net) { online_->act(Action{Action::Kind::Move, cell}); return; }
        battle_turn(dex_, world_, Action{Action::Kind::Move, cell});
        narrate();
        return;
    }
    if (in_party_) {
        if (cell >= kPartySize) return;
        const Party& p = mine_side;
        if (cell == p.active)            { say("Already out"); return; }
        if (!p.member[cell].alive())     { say("It has fainted"); return; }
        in_party_ = false;
        if (net) { online_->act(Action{Action::Kind::Switch, cell}); return; }
        battle_turn(dex_, world_, Action{Action::Kind::Switch, cell});
        narrate();
        return;
    }
    if (net) {
        // A rated match has no ball and no running away: a wild creature can be caught
        // and a trainer's cannot, and "Run" against a person is a forfeit the protocol
        // has no frame for. Both refusals are SAID rather than silently ignored.
        switch (cell) {
            case 0: in_moves_ = true; break;
            case 1: say("No balls in a rated match"); break;
            case 2: in_party_ = true; break;
            case 3: say("You cannot run from a rated match"); break;
            default: break;
        }
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
    in_ = input;
    if (!ready_) return;
    for (auto& [name, sheet] : tiles_) {
        (void)name;
        sheet.update(static_cast<float>(dt));
    }
    // The session is pumped BEFORE the input is read, so the mode the pointer is tested
    // against is the one the last frame drew. A tap resolved against a mode that
    // changed between the pump and the read is a tap on a button that is not there.
    if (online_) {
        online_->update();
        if (online_->state() == PvpClient::State::Failed && online_note_.empty())
            online_note_ = online_->problem();
    }
    const bool was_fighting = world_.phase == Phase::Battle;
    update_world(dt, input);
    if (was_fighting && world_.phase != Phase::Battle) write_tape();
}

void CreaturesScene::update_world(double dt, const platform::InputState& input) {
    if (message_t_ > 0) message_t_ -= dt;
    if (step_cool_ > 0) step_cool_ -= dt;

    const touch::PointerSet pointers = touch::pointers(input);
    const Mode m = mode();
    const Press press = read(layout(fb_w_, fb_h_, m), m,
                             pointers.data, pointers.count);

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
        if (press.online || hit(platform::Key::O)) {
            if (!start_online()) say("Cannot start a match now");
        }
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

    if (m == Mode::Online) {
        // Cancel is the only control, and it is also Escape. A player who leaves must
        // leave the SERVER's queue too, which is what PvpClient::cancel does.
        if (press.back || hit(platform::Key::X) || hit(platform::Key::Escape))
            cancel_online();
        return;
    }

    if (m == Mode::Ack) {
        if (press.ack || hit(platform::Key::Z) || hit(platform::Key::Enter) ||
            hit(platform::Key::Space)) {
            // A rated match ends by dropping the session, not by ending a wild battle
            // — `world_.phase` never left Overworld, and calling end_battle here would
            // heal the party as though a blackout had happened.
            if (online_) { online_.reset(); online_ack_ = true; return; }
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
    else                           render_battle(ctx);   // Online included: same screen
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
    const Layout l = layout(fb_w_, fb_h_, Mode::Overworld);
    const Creature& lead = world_.party.now();
    const SpeciesDef* s = dex_.species_by_id(lead.species);
    if (!l.hud.empty()) {
        g.fill_round_rect(l.hud.x, l.hud.y, l.hud.w, l.hud.h, th::radius_md, th::elevated);
        g.draw_round_rect(l.hud.x, l.hud.y, l.hud.w, l.hud.h, th::radius_md, th::border);

        g.set_font_size(th::sz_caption);
        const std::string identity = display_name(s ? s->name : "-") + std::string("  Lv") +
                                     std::to_string(lead.level);
        g.draw_text(l.hud.x + 8, l.hud.y + 6, identity.c_str(), th::text);
        const std::string hp = std::to_string(std::max(0, lead.hp)) + "/" +
                               std::to_string(std::max(0, lead.max_hp));
        const int hpw = g.text_width(hp.c_str());
        g.draw_text(l.health.x + l.health.w - hpw, l.hud.y + 6, hp.c_str(), th::text_dim);
        g.fill_round_rect(l.health.x, l.health.y, l.health.w, l.health.h,
                          l.health.h / 2, th::track);
        const int health_fill = lead.max_hp > 0
            ? (l.health.w - 2) * std::clamp(lead.hp, 0, lead.max_hp) / lead.max_hp : 0;
        if (health_fill > 0)
            g.fill_round_rect(l.health.x + 1, l.health.y + 1, health_fill,
                              l.health.h - 2, (l.health.h - 2) / 2,
                              lead.hp * 4 <= lead.max_hp ? th::danger : th::success);

        char stats[96];
        std::snprintf(stats, sizeof stats, "BALLS %d     WINS %d     CAUGHT %d",
                      world_.balls, world_.wins, world_.caught);
        g.draw_text(l.stats.x, l.stats.y + 7, stats, th::text_dim);
    } else {
        char line[128];
        std::snprintf(line, sizeof line, "%s  Lv%d  %d/%d  balls %d  won %d  caught %d",
                      s ? s->name.c_str() : "-", lead.level, lead.hp, lead.max_hp,
                      world_.balls, world_.wins, world_.caught);
        g.fill_rect(0, 0, fb_w_, 22, th::bg);
        g.set_font_size(th::sz_caption);
        g.draw_text(8, 6, line, th::text);
    }
    if (message_t_ > 0 && !l.message.empty()) {
        g.fill_round_rect(l.message.x, l.message.y, l.message.w, l.message.h,
                          th::radius_sm, th::elevated);
        g.draw_round_rect(l.message.x, l.message.y, l.message.w, l.message.h,
                          th::radius_sm, th::border_strong);
        g.set_font_size(th::sz_caption);
        g.draw_text(l.message.x + 10, l.message.y + 10, message_.c_str(), th::text);
    }
    render_controls(g);
}

void CreaturesScene::render_battle(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const Layout l = layout(fb_w_, fb_h_, mode());

    g.clear(gfx::rgb(0x1b, 0x22, 0x2c));
    g.fill_rect(0, 0, fb_w_, fb_h_ / 2, gfx::rgb(0x2e, 0x3d, 0x4e));
    g.fill_rect(0, fb_h_ / 2, fb_w_, fb_h_ / 2, gfx::rgb(0x24, 0x33, 0x26));

    const auto bar = [&](const Box& card, const Creature& c, const char* who) {
        if (card.empty()) return;
        const SpeciesDef* s = dex_.species_by_id(c.species);
        char t[96];
        const std::string name = display_name(s ? s->name : "-");
        std::snprintf(t, sizeof t, "%s  %s  Lv%d", who, name.c_str(), c.level);
        g.fill_round_rect(card.x, card.y, card.w, card.h, th::radius_sm, th::elevated);
        g.draw_round_rect(card.x, card.y, card.w, card.h, th::radius_sm, th::border);
        g.set_font_size(th::sz_caption);
        g.draw_text(card.x + 8, card.y + 7, t, th::text);
        const int bw = card.w - 16, bh = 7;
        const int bx = card.x + 8, by = card.y + 32;
        g.fill_round_rect(bx, by, bw, bh, bh / 2, th::track);
        const int fill = c.max_hp > 0 ? (bw - 2) * std::clamp(c.hp, 0, c.max_hp) / c.max_hp : 0;
        const gfx::Color hue = c.hp * 4 <= c.max_hp ? th::danger
                             : c.hp * 2 <= c.max_hp ? th::warn : th::success;
        if (fill > 0) g.fill_round_rect(bx + 1, by + 1, fill, bh - 2, (bh - 2) / 2, hue);
        const std::string hp = std::to_string(std::max(0, c.hp)) + "/" +
                               std::to_string(std::max(0, c.max_hp));
        const int hpw = g.text_width(hp.c_str());
        g.draw_text(card.x + card.w - hpw - 8, card.y + 7, hp.c_str(), th::text_dim);
        if (c.status != Status::None) {
            static const char* kTag[4] = {"", "BRN", "PAR", "SLP"};
            g.draw_text(card.x + card.w - 32, card.y + 20,
                        kTag[static_cast<int>(c.status)], th::warn);
        }
    };

    // ---- looking for an opponent: no creatures yet, one line and Cancel -------
    if (mode() == Mode::Online) {
        const PvpClient::State st = online_ ? online_->state() : PvpClient::State::Idle;
        const char* line = st == PvpClient::State::SigningIn  ? "Signing in..."
                         : st == PvpClient::State::Connecting ? "Connecting..."
                         : st == PvpClient::State::Queued     ? "Looking for an opponent..."
                         : st == PvpClient::State::Reporting  ? "Reporting the result..."
                                                              : "Starting...";
        g.fill_rect(l.panel.x, l.panel.y, l.panel.w, l.panel.h, th::bg);
        g.draw_line(l.panel.x, l.panel.y, l.panel.x + l.panel.w, l.panel.y, th::border);
        g.set_font_size(th::sz_title);
        g.draw_text(fb_w_ / 2 - 90, fb_h_ / 2 - 30, "Rated match", th::text);
        g.set_font_size(th::sz_body);
        g.draw_text(l.log.x, l.log.y + 4, line, th::text);
        if (!online_note_.empty())
            g.draw_text(l.log.x, l.log.y + 26, online_note_.c_str(), th::warn);
        if (!l.back.empty()) {
            g.fill_round_rect(l.back.x, l.back.y, l.back.w, l.back.h,
                              th::radius_sm, th::ctrl);
            g.draw_round_rect(l.back.x, l.back.y, l.back.w, l.back.h,
                              th::radius_sm, th::border_strong);
            g.draw_text(l.back.x + 12, l.back.y + l.back.h / 2 - 6, "Cancel", th::text);
        }
        render_controls(g);
        return;
    }

    const bool net = online_ != nullptr;
    const Battle& shown = shown_battle();
    // A session that failed before a battle began has nothing to draw: no parties were
    // exchanged, so both sides are species 0 and the screen showed two blank placeholder
    // squares over two empty health bars. `net_battle_live()` is the same question
    // `shown_battle()` asks, which is why it is one function and not two conditions.
    const bool have_creatures = !net || net_battle_live();
    const Creature& mine = shown.side[my_side()].now();
    const Creature& them = shown.side[1 - my_side()].now();

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
    if (have_creatures) {
        creature(l.theirs, them);
        creature(l.mine, mine);
        bar(l.theirs_info, them, net ? "RIVAL" : "WILD");
        bar(l.mine_info, mine, "YOUR TEAM");
    } else {
        g.set_font_size(th::sz_title);
        g.draw_text(fb_w_ / 2 - 110, fb_h_ / 2 - 40, "No match", th::text_dim);
        g.set_font_size(th::sz_body);
    }

    // ---- the panel ----
    g.fill_rect(l.panel.x, l.panel.y, l.panel.w, l.panel.h, th::bg);
    g.draw_line(l.panel.x, l.panel.y, l.panel.x + l.panel.w, l.panel.y, th::border);
    g.set_font_size(th::sz_body);
    if (!message_.empty()) g.draw_text(l.log.x, l.log.y + 4, message_.c_str(), th::text);

    char sub[96];
    switch (mode()) {
        case Mode::Ack:
            // The rating is the whole point of a RATED match, so it is on the screen
            // that ends one. `rating_applied()` is a separate fact from the number:
            // both clients report, exactly one report moves the ladder, and a player
            // told "+0" without being told why would read it as a bug.
            //
            // ONE line chosen, then ONE line drawn. The first version of this `break`ed
            // out of the switch after choosing the online text — skipping the draw, the
            // Continue button and the `return` — so the result screen was blank with a
            // button that was hittable and invisible. Every test passed: they tap the
            // rect the LAYOUT reports, and the layout was right. Only the picture showed
            // it, which is now four chapters running.
            if (online_) {
                if (online_->state() == PvpClient::State::Failed)
                    std::snprintf(sub, sizeof sub, "%s", online_->problem().c_str());
                else
                    std::snprintf(sub, sizeof sub, "%s   rating %d (%+d)%s",
                                  online_->net().won()           ? "You won."
                                : online_->net().winner() < 0    ? "A draw."
                                                                 : "You lost.",
                                  online_->rating(), online_->rating_delta(),
                                  online_->rating_applied() ? "" : "  (already counted)");
            } else {
                std::snprintf(sub, sizeof sub, "%s",
                    world_.phase == Phase::Won      ? "It fainted."
                  : world_.phase == Phase::Caught   ? "It joined your party."
                  : world_.phase == Phase::Blackout ? "You blacked out."
                                                    : "You got away.");
            }
            g.draw_text(l.log.x, l.log.y + 26, sub, th::text_dim);
            g.fill_round_rect(l.ack.x, l.ack.y, l.ack.w, l.ack.h,
                              th::radius_sm, th::accent);
            g.draw_round_rect(l.ack.x, l.ack.y, l.ack.w, l.ack.h,
                              th::radius_sm, th::accent_hover);
            g.draw_text(l.ack.x + 28, l.ack.y + 14, "Continue", ink_on(th::accent));
            render_controls(g);
            return;
        default: break;
    }

    for (int i = 0; i < 6; ++i) {
        const Box& b = l.cell[i];
        if (b.empty()) continue;
        std::string label = "-";
        std::string detail;
        gfx::Color tint = th::ctrl;
        if (mode() == Mode::Menu && i < 4) {
            label = kMenu[i];
            static const char* kDetail[4] = {"Choose a move", "Try to catch",
                                              "Switch lead", "Leave battle"};
            detail = kDetail[i];
            if (net) {
                // Drawn DIM rather than hidden: a menu whose shape changes between a
                // wild fight and a rated one teaches two layouts, and a control that
                // vanishes is a control a player looks for. Tapping one says why.
                if (i == 1 || i == 3) tint = th::ctrl_disabled;
            } else if (i == 1) {
                label += " x" + std::to_string(world_.balls);
            }
        } else if (mode() == Mode::Moves && i < kMoveSlots) {
            const MoveSlot& ms = mine.moves[i];
            if (const MoveDef* mv = dex_.move(ms.move)) {
                label = mv->name;
                detail = "PP " + std::to_string(ms.pp);
                tint  = type_colour(mv->type);
            }
        } else if (mode() == Mode::Party && i < kPartySize) {
            const Creature& c = shown.side[my_side()].member[i];
            if (c.species != 0) {
                const SpeciesDef* s = dex_.species_by_id(c.species);
                label = (s ? s->name : "?") + "  " + std::to_string(c.hp) + "/" +
                        std::to_string(c.max_hp);
                tint  = c.alive() ? type_colour(s ? s->type : 0) : th::ctrl;
            }
        }
        g.fill_round_rect(b.x, b.y, b.w, b.h, th::radius_sm, tint);
        g.draw_round_rect(b.x, b.y, b.w, b.h, th::radius_sm, th::border_strong);
        const gfx::Color ink = ink_on(tint);
        if (!detail.empty() && b.h >= 36) {
            g.set_font_size(th::sz_body);
            g.draw_text(b.x + 8, b.y + 6, label.c_str(), ink);
            g.set_font_size(th::sz_caption);
            g.draw_text(b.x + 8, b.y + b.h - 16, detail.c_str(),
                        tint == th::ctrl_disabled ? th::text_muted : ink);
        } else {
            g.set_font_size(th::sz_body);
            g.draw_text(b.x + 8, b.y + b.h / 2 - 6, label.c_str(), ink);
        }
    }
    if (!l.back.empty()) {
        g.fill_round_rect(l.back.x, l.back.y, l.back.w, l.back.h,
                          th::radius_sm, th::ctrl);
        g.draw_round_rect(l.back.x, l.back.y, l.back.w, l.back.h,
                          th::radius_sm, th::border_strong);
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
                     "creatures: controls %dx%d up=%s down=%s left=%s right=%s act=%s "
                     "save=%s online=%s\n",
                     fb_w_, fb_h_, box(l.up).c_str(), box(l.down).c_str(),
                     box(l.left).c_str(), box(l.right).c_str(),
                     box(l.act).c_str(), box(l.save).c_str(), box(l.online).c_str());
    }

    // A SECOND line, the first time a battle is on screen. The farm needed one line
    // because everything it can do is on one screen; this game has two, and the
    // browser check cannot finish a fight it cannot aim at. Printed from here, at the
    // moment the screen exists, for the same reason as the first: the numbers a
    // checker uses have to be the numbers the renderer used.
    // Gated on the CELLS existing, not merely on "not the overworld". Chapter 146 added
    // a second non-overworld mode with no cells at all (looking for an opponent), and
    // this line fired on it first — announcing `fight=0,0,0,0` to a browser check whose
    // whole job is to aim at those numbers. A diagnostic that describes the wrong screen
    // is worse than none: it is a set of coordinates that look usable.
    static bool announced_battle = false;
    if (!announced_battle && !l.panel.empty() && !l.cell[0].empty()) {
        announced_battle = true;
        std::fprintf(stderr,
                     "creatures: battle %dx%d fight=%s ball=%s party=%s run=%s ack=%s back=%s\n",
                     fb_w_, fb_h_, box(l.cell[0]).c_str(), box(l.cell[1]).c_str(),
                     box(l.cell[2]).c_str(), box(l.cell[3]).c_str(),
                     box(layout(fb_w_, fb_h_, Mode::Ack).ack).c_str(), box(l.back).c_str());
    }

    if (!l.pad_visible()) return;
    const touch::PointerSet pointers = touch::pointers(in_);
    const auto btn = [&](const Box& b, const char* label) {
        if (b.empty()) return;
        const bool hot = pointers.down_in(b);
        g.fill_round_rect(b.x, b.y, b.w, b.h, th::radius_md,
                          hot ? th::ctrl_press : th::ctrl);
        g.draw_round_rect(b.x, b.y, b.w, b.h, th::radius_md,
                          hot ? th::accent : th::border_strong);
        g.set_font_size(th::sz_caption);
        const int tw = g.text_width(label);
        g.draw_text(b.x + (b.w - tw) / 2, b.y + (b.h - th::sz_caption) / 2,
                    label, hot ? th::text : th::text_dim);
    };
    btn(l.up, label(Control::Up)); btn(l.down, label(Control::Down));
    btn(l.left, label(Control::Left)); btn(l.right, label(Control::Right));
    btn(l.act, label(Control::Act)); btn(l.save, label(Control::Save));
    btn(l.online, label(Control::Online));
}

} // namespace creature
