// =============================================================================
//  games/hub/hub_scene.cpp  —  draw the hub lines into a window
// =============================================================================
#include "games/hub/hub_scene.hpp"

#include <utility>

#include "engine/color.hpp"
#include "engine/hub/hub_build.hpp"
#include "engine/release/ops.hpp"

namespace hubui {

HubScene::HubScene(std::string project_path)
    : path_(std::move(project_path)), known_entries_{"fps"} {
    rebuild();
}

void HubScene::rebuild() { view_ = engine::build_hub_view(path_, known_entries_); }

void HubScene::update(double dt, const platform::InputState& in) {
    if (flash_t_ > 0) flash_t_ -= dt;

    // The Hub as a controller: keys drive the shared engine::release ops, then refresh.
    auto did = [&](const engine::OpResult& r) { flash_ = r.message; flash_t_ = 5.0; rebuild(); };
    if      (in.pressed(platform::Key::Space)) did(engine::publish(path_, "development", "hub", known_entries_));
    else if (in.pressed(platform::Key::Num1))  did(engine::promote("development", "preview", "hub"));
    else if (in.pressed(platform::Key::Num2))  did(engine::promote("preview", "production", "hub"));
    else if (in.pressed(platform::Key::R))     rebuild();   // pick up external edits
}

void HubScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    g.clear(gfx::rgb(14, 16, 22));
    g.set_font(ctx.font, 18);

    if (!view_) {
        g.draw_text(24, 44, ("hub: cannot read '" + path_ + "'").c_str(), gfx::rgb(255, 120, 120));
        return;
    }

    const auto lines = engine::hub_lines(*view_);
    int y = 44;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& s = lines[i];
        gfx::Color c = gfx::rgb(210, 216, 228);
        if (i == 0) { g.set_font_size(26); c = gfx::rgb(255, 255, 255); }   // title
        else        { g.set_font_size(18); }
        if (s.rfind("next:", 0) == 0)                    c = gfx::rgb(120, 220, 140);  // recommendation → green
        else if (s.rfind("  - ", 0) == 0)                c = gfx::rgb(255, 180, 90);   // problem → amber
        else if (s.find("NOT shippable") != std::string::npos) c = gfx::rgb(255, 120, 120);
        g.draw_text(24, y, s.c_str(), c);
        y += (i == 0) ? 42 : 26;
    }

    if (flash_t_ > 0 && !flash_.empty()) {
        g.set_font_size(16);
        g.draw_text(24, h_ - 52, flash_.c_str(), gfx::rgb(120, 220, 140));
    }
    g.set_font_size(14);
    g.draw_text(24, h_ - 26, "Space: publish→dev    1: promote→preview    2: promote→production    R: refresh",
                gfx::rgb(120, 128, 140));
}

} // namespace hubui
