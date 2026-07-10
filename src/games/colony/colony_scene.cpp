// =============================================================================
//  games/colony/colony_scene.cpp  —  colony scene implementation
// =============================================================================
#include "games/colony/colony_scene.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
        if (r) { online_ = true; status_ = "signed in: " + r->display_name; }
        else   { online_ = false; status_ = "login failed"; }
    });
}

void ColonyScene::submit_score() {
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

iso::Vec2i ColonyScene::hovered_cell(const platform::InputState& in) const {
    return iso::screen_to_grid(static_cast<float>(in.mouse_x), static_cast<float>(in.mouse_y), ox_, oy_);
}

void ColonyScene::update(double dt, const platform::InputState& /*in*/) {
    client_.update();   // BaaS: pump async responses → fire callbacks (even while paused)
    if (!running_) return;
    // Apply the UI speed to every agent, then advance (parallel inside the Sim).
    sim_.registry().view<Agent>([&](ecs::Entity, Agent& a) { a.speed = speed_; });
    sim_.update(dt);
}

void ColonyScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    w_ = g.width();
    h_ = g.height();
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
    ui_.panel(ui::Rect{12, 12, 210, 300}, "COLONY");
    char line[64];
    std::snprintf(line, sizeof(line), "agents: %d   fps: %d", sim_.agent_count(), static_cast<int>(fps_ + 0.5));
    ui_.label(line);
    if (ui_.button("Spawn agent")) sim_.spawn_agent(1, sim_.height() / 2, gfx::rgb(90, 180, 235));
    if (ui_.button("Send to corner")) sim_.set_goal(sim_.width() - 2, sim_.height() - 6);
    ui_.checkbox("running", running_);
    ui_.slider("speed", speed_, 0.5f, 8.0f);
    if (ui_.button("Reset")) { sim_.reset_default(); running_ = true; }

    // ---- BaaS: online controls ----
    ui_.label(status_.c_str());
    char sc[64];
    std::snprintf(sc, sizeof(sc), "score: %ld   rank: %d", my_score_, my_rank_);
    ui_.label(sc);
    if (online_) {
        if (ui_.button("Submit score")) submit_score();
        if (ui_.button(board_open_ ? "Hide leaderboard" : "Leaderboard")) {
            if (board_open_) board_open_ = false; else refresh_board();
        }
    } else {
        if (ui_.button("Login (guest)")) login();
    }
    ui_.end();

    // ---- BaaS: the leaderboard panel (raw-drawn, read-only) ----
    const int  bx = 240, by = 12, bw = 264;
    const int  rows = board_.entries.empty() ? 1 : static_cast<int>(board_.entries.size());
    const int  bh = 30 + rows * 14 + 8;
    const bool over_board = board_open_ && ctx.input.mouse_x >= bx && ctx.input.mouse_x < bx + bw &&
                            ctx.input.mouse_y >= by && ctx.input.mouse_y < by + bh;
    if (board_open_) {
        g.fill_rect(bx, by, bw, bh, gfx::rgb(20, 24, 34));
        g.draw_text(bx + 8, by + 8, "LEADERBOARD  colony_high", gfx::rgb(220, 220, 120), 1);
        int yy = by + 26;
        if (board_.entries.empty()) {
            g.draw_text(bx + 8, yy, "no scores yet", gfx::rgb(150, 160, 180), 1);
        }
        for (const auto& e : board_.entries) {
            char row[80];
            std::snprintf(row, sizeof(row), "%2d. %-12.12s %ld", e.rank, e.display_name.c_str(), e.value);
            g.draw_text(bx + 8, yy, row, gfx::rgb(200, 210, 225), 1);
            yy += 14;
        }
    }

    // ---- click the world to send every agent to the cursor (A*) ----
    if (ctx.input.pressed(MouseButton::Left) && !ui_.hovering_ui() && !over_board &&
        hovered_.x >= 0 && hovered_.y >= 0 && hovered_.x < sim_.width() && hovered_.y < sim_.height())
        sim_.set_goal(hovered_.x, hovered_.y);

    g.draw_text(12, h_ - 16,
                "click: send agents (A*)  -  B:ECS C:jobs A:frame D:asset F:ui  +  BaaS: auth/leaderboard  -  ESC:quit",
                gfx::rgb(150, 160, 180), 1);
}

} // namespace colony
