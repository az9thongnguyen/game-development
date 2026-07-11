// =============================================================================
//  games/colony/colony_scene.cpp  —  colony scene implementation
// =============================================================================
#include "games/colony/colony_scene.hpp"

#include "engine/ui/theme.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "engine/assets.hpp"
#include "engine/color.hpp"

namespace colony {

using platform::MouseButton;

namespace {
constexpr const char* kSpritePath = "colony_agent.hrt";

gfx::Color terrain_color(iso::Terrain t) {
    switch (t) {
        case iso::Terrain::Grass: return gfx::rgb(104, 168, 76);
        case iso::Terrain::Soil:  return gfx::rgb(150, 110, 68);
        case iso::Terrain::Water: return gfx::rgb(70, 120, 200);
        case iso::Terrain::Path:  return gfx::rgb(160, 156, 140);
    }
    return gfx::rgb(104, 168, 76);
}

void fill_diamond(gfx::Renderer2D& g, int cx, int cy, int hw, int hh, gfx::Color c) {
    if (hh <= 0) return;
    for (int dy = -hh; dy <= hh; ++dy) {
        const int half = static_cast<int>(static_cast<float>(hw) *
                         (1.0f - static_cast<float>(std::abs(dy)) / static_cast<float>(hh)));
        for (int x = cx - half; x <= cx + half; ++x) g.set_pixel(x, cy + dy, c);
    }
}

// Generate a tiny 16x16 agent sprite as raw .hrt bytes (a soft round blob), so the
// asset cache + hot reload have something to load without any bundled file.
std::vector<uint8_t> gen_agent_sprite() {
    constexpr int S = 16;
    std::vector<uint8_t> b = {'H', 'R', 'T', '1'};
    auto be = [&](uint32_t x) {
        b.push_back(static_cast<uint8_t>(x >> 24)); b.push_back(static_cast<uint8_t>(x >> 16));
        b.push_back(static_cast<uint8_t>(x >> 8));  b.push_back(static_cast<uint8_t>(x));
    };
    be(S); be(S);
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const float dx = static_cast<float>(x) - 7.5f, dy = static_cast<float>(y) - 7.5f;
            const float d  = std::sqrt(dx * dx + dy * dy);
            if (d <= 7.0f) {                                  // body (darker ring at the edge)
                const bool ring = d > 5.5f;
                b.push_back(ring ? 60 : 90);
                b.push_back(ring ? 120 : 180);
                b.push_back(ring ? 170 : 235);
                b.push_back(255);
            } else {
                b.push_back(0); b.push_back(0); b.push_back(0); b.push_back(0);   // transparent
            }
        }
    return b;
}
} // namespace

ColonyScene::ColonyScene()
    : sim_(14, 14), frame_(64 * 1024),
      client_(gbaas::Config{default_base_url(), "pk_demo_colony"}) {
    sim_.reset_default();

    // D: ensure the sprite exists, register a bytes→Image loader, load via the cache.
    if (!assets::load_file(kSpritePath)) assets::write_file(kSpritePath, gen_agent_sprite());
    cache_.register_loader<gfx::Image>([](const std::vector<uint8_t>& bytes) -> std::shared_ptr<gfx::Image> {
        auto img = gfx::decode_hrt(bytes);
        return img ? std::make_shared<gfx::Image>(std::move(*img)) : nullptr;
    });
    sprite_ = cache_.load<gfx::Image>(kSpritePath);

    login();   // BaaS: guest sign-in (async; completes over the next few frames)
}

// Native talks to a local baas; the web build is served BY the baas (same origin),
// so a relative base ("") resolves API calls against the page.
std::string ColonyScene::default_base_url() {
#ifdef __EMSCRIPTEN__
    return "";
#else
    return "http://127.0.0.1:8080";
#endif
}

void ColonyScene::login() {
    status_ = "connecting...";
    client_.auth().guest([this](gbaas::Result<gbaas::Session> r) {
        if (r) {
            online_ = true;
            status_ = "signed in: " + r->display_name;
            refresh_wood();
            // remote config (motd) + currently-active live events
            client_.config().get("motd", [this](gbaas::Result<std::string> cr) {
                if (cr) motd_ = *cr;
            });
            client_.events().active([this](gbaas::Result<std::vector<gbaas::LiveEvent>> er) {
                if (er && !er->empty()) event_name_ = (*er)[0].name;
            });
            connect_realtime();   // open the realtime channel + join the shared room
        } else {
            online_ = false;
            status_ = "login failed";
        }
    });
}

// Open the persistent WebSocket channel and join a shared "colony" room, so every
// online player sees the others' presence. Native uses libcurl ws://; the web build
// uses the browser WebSocket — same call. Fails gracefully (rt_line_ says so).
void ColonyScene::connect_realtime() {
    if (client_.realtime().connect()) {
        client_.realtime().join("colony");
        rt_line_ = "realtime: connecting...";
    } else {
        rt_line_ = "realtime: unavailable";   // e.g. SDK built without a ws-capable curl
    }
}

// Drain realtime events each frame → keep the presence count + last message fresh.
void ColonyScene::poll_realtime() {
    gbaas::RtEvent e;
    while (client_.realtime().poll(e)) {
        if (e.ev == "connected") {
            rt_on_ = true;
        } else if (e.ev == "joined") {
            peers_ = static_cast<int>(e.members.size());
        } else if (e.ev == "peer_joined") {
            ++peers_;
        } else if (e.ev == "peer_left") {
            if (peers_ > 0) --peers_;
        } else if (e.ev == "msg") {
            last_msg_ = e.name + ": " + e.data;
        } else if (e.ev == "disconnected") {
            rt_on_ = false;
            peers_ = 0;
        }
        if (rt_on_) rt_line_ = "realtime: on (" + std::to_string(peers_) + " here)";
        else if (rt_line_ != "realtime: unavailable") rt_line_ = "realtime: off";
    }
}

// ---- replay -----------------------------------------------------------------
// A replay is the recorded command stream (one "relframe:cmd" line per action).
// Playing it back re-issues the same commands at the same relative frames on a
// fresh sim — a command-stream replay, the model RTS games use.
void ColonyScene::apply_command(const std::string& cmd) {
    if (cmd == "spawn")       sim_.spawn_agent(1, sim_.height() / 2, gfx::rgb(90, 180, 235));
    else if (cmd == "corner") sim_.set_goal(sim_.width() - 2, sim_.height() - 6);
    else if (cmd == "reset")  { sim_.reset_default(); running_ = true; }
}

void ColonyScene::issue_command(const std::string& cmd) {
    apply_command(cmd);
    if (recording_) rec_buf_ += std::to_string(sim_frame_ - rec_start_) + ":" + cmd + "\n";
}

void ColonyScene::save_replay() {
    recording_ = false;
    if (rec_buf_.empty()) { replay_line_ = "replay: nothing recorded"; return; }
    if (!online_)         { replay_line_ = "replay: login first";      return; }
    const std::string name = "colony-" + std::to_string(sim_frame_);
    client_.replays().save(name, rec_buf_, [this](gbaas::Result<gbaas::ReplayMeta> r) {
        replay_line_ = r ? ("replay: saved #" + std::to_string(r->id)) : "replay: save failed";
    });
    rec_buf_.clear();
}

void ColonyScene::play_last_replay() {
    if (!online_) { replay_line_ = "replay: login first"; return; }
    replay_line_ = "replay: loading...";
    client_.replays().list([this](gbaas::Result<std::vector<gbaas::ReplayMeta>> r) {
        if (!r || r->empty()) { replay_line_ = "replay: none saved"; return; }
        const long long id = (*r)[0].id;   // newest first
        client_.replays().get(id, [this](gbaas::Result<gbaas::Replay> g) {
            if (!g) { replay_line_ = "replay: load failed"; return; }
            play_cmds_.clear();
            const std::string& d = g->data;
            for (std::size_t pos = 0; pos < d.size();) {
                std::size_t nl = d.find('\n', pos);
                if (nl == std::string::npos) nl = d.size();
                const std::string line = d.substr(pos, nl - pos);
                pos = nl + 1;
                const std::size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                const long f = std::strtol(line.substr(0, colon).c_str(), nullptr, 10);
                play_cmds_.emplace_back(f, line.substr(colon + 1));
            }
            sim_.reset_default();   // play on a fresh sim
            running_ = true;
            playing_ = true; play_idx_ = 0; play_frame_ = 0;
            replay_line_ = "replay: playing (" + std::to_string(play_cmds_.size()) + " cmds)";
        });
    });
}

void ColonyScene::update_replay() {
    if (!playing_) return;
    ++play_frame_;
    while (play_idx_ < play_cmds_.size() && play_cmds_[play_idx_].first <= play_frame_) {
        apply_command(play_cmds_[play_idx_].second);
        ++play_idx_;
    }
    if (play_idx_ >= play_cmds_.size()) { playing_ = false; replay_line_ = "replay: done"; }
}

void ColonyScene::submit_score() {
    client_.analytics().track("score.submitted");   // fire-and-forget analytics event
    // The colony's "score" is how many colonists it is managing.
    client_.leaderboard("colony_high").submit(sim_.agent_count(),
        [this](gbaas::Result<gbaas::Rank> r) {
            if (r) { my_score_ = r->value; my_rank_ = r->rank; status_ = "score submitted"; refresh_board(); }
            else   { status_ = "submit failed"; }
        });
}

void ColonyScene::refresh_board() {
    board_open_ = true;
    client_.leaderboard("colony_high").top(10, [this](gbaas::Result<gbaas::Board> r) {
        if (r) board_ = *r;
    });
}

// Serialize every entity (grid pos, color, agent-or-prop) to a small JSON string
// and store it under the "colony" slot. Payload is UTF-8 JSON — cloud save's format.
void ColonyScene::cloud_save() {
    std::string s     = "{\"e\":[";
    bool        first = true;
    sim_.registry().view<Visual, GridPos>([&](ecs::Entity, Visual& v, GridPos& p) {
        if (!first) s += ',';
        first = false;
        s += "{\"x\":" + std::to_string(static_cast<int>(std::lround(p.x))) +
             ",\"y\":" + std::to_string(static_cast<int>(std::lround(p.y))) +
             ",\"c\":" + std::to_string(static_cast<unsigned long>(v.color)) +
             ",\"a\":" + (v.is_agent ? "1" : "0") + "}";
    });
    s += "]}";
    client_.saves().put("colony", s, [this](gbaas::Result<gbaas::SaveMeta> r) {
        status_ = r ? ("cloud saved v" + std::to_string(r->version)) : "save failed";
    });
}

// Load the "colony" slot and rebuild the sim from scratch.
void ColonyScene::cloud_load() {
    client_.saves().get("colony", [this](gbaas::Result<gbaas::Save> r) {
        if (!r) { status_ = "no cloud save"; return; }
        const auto j = gbaas::json::parse(r->data);
        if (!j) { status_ = "bad save data"; return; }
        sim_.clear();
        const auto& es = (*j)["e"];
        for (std::size_t k = 0; k < es.size(); ++k) {
            const auto&      it = es[k];
            const int        x  = static_cast<int>(it["x"].as_int());
            const int        y  = static_cast<int>(it["y"].as_int());
            const gfx::Color c  = static_cast<gfx::Color>(it["c"].as_int());
            if (it["a"].as_int() != 0) sim_.spawn_agent(x, y, c);
            else                       sim_.spawn_prop(x, y, c);
        }
        status_ = "cloud loaded v" + std::to_string(r->version);
    });
}

void ColonyScene::refresh_wood() {
    client_.inventory().get("wood", [this](gbaas::Result<gbaas::Item> r) {
        if (r) wood_ = r->qty;
    });
}

iso::Vec2i ColonyScene::hovered_cell(const platform::InputState& in) const {
    return iso::screen_to_grid(static_cast<float>(in.mouse_x), static_cast<float>(in.mouse_y), ox_, oy_);
}

void ColonyScene::update(double dt, const platform::InputState& /*in*/) {
    client_.update();   // BaaS: pump async responses → fire callbacks (even while paused)
    poll_realtime();    // drain realtime (Lobby) events → presence state
    ++sim_frame_;
    update_replay();    // if playing a replay, re-issue recorded commands on schedule
    if (!running_) return;
    // Apply the UI speed to every agent, then advance (parallel inside the Sim).
    sim_.registry().view<Agent>([&](ecs::Entity, Agent& a) { a.speed = speed_; });
    sim_.update(dt);
}

void ColonyScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width();
    h_ = g.height();
    g.set_font(ctx.font, ui::theme::sz_body);   // AA UI text (falls back to 8x8 if null)
    if (ctx.dt > 0.0) fps_ = fps_ * 0.92 + (1.0 / ctx.dt) * 0.08;

    cache_.reload_changed();                 // D: hot-reload the sprite if the file changed
    hovered_ = hovered_cell(ctx.input);

    g.clear(gfx::rgb(38, 44, 60));

    // ---- ground tiles, back-to-front ----
    for (int gy = 0; gy < sim_.height(); ++gy)
        for (int gx = 0; gx < sim_.width(); ++gx) {
            const iso::ScreenPt s = iso::grid_to_screen(static_cast<float>(gx), static_cast<float>(gy), ox_, oy_);
            fill_diamond(g, static_cast<int>(s.x), static_cast<int>(s.y),
                         iso::kTileW / 2, iso::kTileH / 2, terrain_color(sim_.map().at(gx, gy)));
        }

    // ---- A: build the depth-sorted drawable list in FRAME scratch ----
    struct Drawable { float key, gx, gy; gfx::Color color; bool is_agent; };
    frame_.flip();
    ecs::Registry& reg = sim_.registry();
    std::size_t count = 0;
    reg.view<Visual, GridPos>([&](ecs::Entity, Visual&, GridPos&) { ++count; });
    if (count > 0) {
        // NOTE: no ECS structural change (spawn/destroy) may occur between this count
        // pass and the fill pass below — the `k < count` guard makes a mismatch safe.
        Drawable* draw = frame_.alloc<Drawable>(count);
        assert(draw);
        std::size_t k = 0;
        reg.view<Visual, GridPos>([&](ecs::Entity, Visual& v, GridPos& p) {
            if (k < count) draw[k++] = {iso::depth_key(p.x, p.y), p.x, p.y, v.color, v.is_agent};
        });
        std::sort(draw, draw + k, [](const Drawable& a, const Drawable& b) { return a.key < b.key; });

        for (std::size_t i = 0; i < k; ++i) {
            const Drawable& d = draw[i];
            const iso::ScreenPt s = iso::grid_to_screen(d.gx, d.gy, ox_, oy_);
            const int cx = static_cast<int>(s.x), cy = static_cast<int>(s.y);
            if (d.is_agent && sprite_ && !sprite_->pixels.empty()) {
                g.blit(gfx::Sprite{sprite_->pixels.data(), sprite_->w, sprite_->h},
                       cx - sprite_->w / 2, cy - sprite_->h + 4);   // feet near the tile center
            } else {
                fill_diamond(g, cx, cy - 6, 14, 8, d.color);        // prop: a small mound/box
                fill_diamond(g, cx, cy - 14, 12, 7, d.color);
            }
        }
    }

    // ---- F: the immediate-mode panel ----
    ui::Input in;
    in.mx = ctx.input.mouse_x; in.my = ctx.input.mouse_y;
    in.down = ctx.input.down(MouseButton::Left);
    in.pressed = ctx.input.pressed(MouseButton::Left);
    in.released = ctx.input.released(MouseButton::Left);

    ui_.begin(&g, in);
    ui_.panel(ui::Rect{12, 12, 236, 736}, "COLONY");
    char line[64];
    std::snprintf(line, sizeof(line), "agents: %d   fps: %d", sim_.agent_count(), static_cast<int>(fps_ + 0.5));
    ui_.label(line);
    if (ui_.button("Spawn agent")) issue_command("spawn");
    if (ui_.button("Send to corner")) issue_command("corner");
    ui_.checkbox("running", running_);
    ui_.slider("speed", speed_, 0.5f, 8.0f);
    if (ui_.button("Reset")) issue_command("reset");

    // ---- BaaS: online controls ----
    ui_.label(status_.c_str());
    char sc[64];
    std::snprintf(sc, sizeof(sc), "score: %lld   rank: %d", my_score_, my_rank_);
    ui_.label(sc);
    if (online_) {
        if (ui_.button("Submit score")) submit_score();
        if (ui_.button(board_open_ ? "Hide leaderboard" : "Leaderboard")) {
            if (board_open_) board_open_ = false; else refresh_board();
        }
        if (ui_.button("Cloud Save")) cloud_save();
        if (ui_.button("Load"))       cloud_load();

        char wl[48];
        std::snprintf(wl, sizeof(wl), "wood: %lld", wood_);
        ui_.label(wl);
        if (ui_.button("Gather +5 wood"))
            client_.inventory().grant("wood", 5, [this](gbaas::Result<gbaas::Item> r) {
                if (r) wood_ = r->qty;
            });
        if (ui_.button("Build -10 wood"))
            client_.inventory().consume("wood", 10, [this](gbaas::Result<gbaas::Item> r) {
                if (r) { wood_ = r->qty; status_ = "built!"; }
                else   { status_ = "not enough wood"; }
            });

        // ---- realtime (Lobby) presence ----
        ui_.label(rt_line_.c_str());
        if (rt_on_ && ui_.button("Ping room")) client_.realtime().send("ping");
        if (!last_msg_.empty()) ui_.label(last_msg_.c_str());

        // ---- replay: record the command stream → cloud → play it back ----
        if (ui_.button(recording_ ? "Stop & Save replay" : "Record replay")) {
            if (recording_) {
                save_replay();
            } else {
                recording_   = true;
                rec_start_   = sim_frame_;
                rec_buf_.clear();
                replay_line_ = "replay: recording";
            }
        }
        if (ui_.button("Play last replay")) play_last_replay();
        ui_.label(replay_line_.c_str());
    } else {
        if (ui_.button("Login (guest)")) login();
    }
    ui_.end();

    // ---- BaaS: the leaderboard panel (read-only, same design language) ----
    const int  bx = 260, by = 12, bw = 288;
    const int  rows = board_.entries.empty() ? 1 : static_cast<int>(board_.entries.size());
    const int  bh = 44 + rows * 20 + ui::theme::space_md;
    const bool over_board = board_open_ && ctx.input.mouse_x >= bx && ctx.input.mouse_x < bx + bw &&
                            ctx.input.mouse_y >= by && ctx.input.mouse_y < by + bh;
    if (board_open_) {
        g.drop_shadow(bx, by, bw, bh, ui::theme::radius_md, 0, 4, 10, gfx::rgba(0, 0, 0, 90));
        g.fill_round_rect(bx, by, bw, bh, ui::theme::radius_md, ui::theme::elevated);
        g.draw_round_rect(bx, by, bw, bh, ui::theme::radius_md, ui::theme::border);
        const int px = bx + ui::theme::space_md;
        g.set_font_size(ui::theme::sz_title);
        g.draw_text(px, by + ui::theme::space_md, "Leaderboard", ui::theme::text);
        g.fill_rect(px, by + 36, bw - 2 * ui::theme::space_md, 1, ui::theme::border);
        int yy = by + 44;
        g.set_font_size(ui::theme::sz_body);
        if (board_.entries.empty()) {
            g.draw_text(px, yy, "no scores yet", ui::theme::text_muted);
        }
        for (const auto& e : board_.entries) {
            char row[80];
            std::snprintf(row, sizeof(row), "%2d.  %-12.12s  %lld", e.rank, e.display_name.c_str(), e.value);
            g.draw_text(px, yy, row, ui::theme::text_dim);
            yy += 20;
        }
    }

    // ---- click the world to send every agent to the cursor (A*) ----
    if (ctx.input.pressed(MouseButton::Left) && !ui_.hovering_ui() && !over_board &&
        hovered_.x >= 0 && hovered_.y >= 0 && hovered_.x < sim_.width() && hovered_.y < sim_.height())
        sim_.set_goal(hovered_.x, hovered_.y);

    if (online_ && (!motd_.empty() || !event_name_.empty())) {
        std::string banner = motd_;
        if (!event_name_.empty()) banner += "   [LIVE: " + event_name_ + "]";
        g.set_font_size(ui::theme::sz_body);
        g.draw_text(bx, h_ - 44, banner.c_str(), ui::theme::warn);
    }
    g.set_font_size(ui::theme::sz_caption);
    g.draw_text(bx, h_ - 22,
                "BaaS: auth / leaderboard / cloud-save / inventory / config / analytics / events   (native + web)   ESC: quit",
                ui::theme::text_muted);
}

} // namespace colony
