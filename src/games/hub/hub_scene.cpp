// =============================================================================
//  games/hub/hub_scene.cpp  —  window glue around the shared Hub panel
// =============================================================================
#include "games/hub/hub_scene.hpp"

#include <utility>

#include "engine/hub/hub_build.hpp"
#include "engine/release/ops.hpp"
#include "engine/ui/theme.hpp"

namespace hubui {

HubScene::HubScene(std::string project_path)
    : path_(std::move(project_path)), known_entries_{"fps"} {
    rebuild();
}

void HubScene::rebuild() { view_ = engine::build_hub_view(path_, known_entries_); }

void HubScene::run(const Action& a) {
    auto did = [&](const engine::OpResult& r) { flash_ = r.message; flash_t_ = 5.0; rebuild(); };
    if      (a.publish)            did(engine::publish(path_, "development", "hub", known_entries_));
    else if (a.promote_preview)    did(engine::promote("development", "preview", "hub"));
    else if (a.promote_production) did(engine::promote("preview", "production", "hub"));
    else if (a.refresh)            rebuild();   // pick up external edits
}

void HubScene::update(double dt, const platform::InputState& in) {
    if (flash_t_ > 0) flash_t_ -= dt;

    // Buttons are resolved during render (that is where the layout is known), so a
    // click arrives here on the following frame; keys are read directly. Both end up
    // in the same run() — one path from intent to operation, whatever triggered it.
    run(pending_);
    pending_ = Action{};

    Action k;
    if      (in.pressed(platform::Key::Space)) k.publish = true;
    else if (in.pressed(platform::Key::Num1))  k.promote_preview = true;
    else if (in.pressed(platform::Key::Num2))  k.promote_production = true;
    else if (in.pressed(platform::Key::R))     k.refresh = true;
    run(k);
}

void HubScene::render(const engine::Context& ctx) {
    namespace th = ui::theme;
    gfx::Renderer2D& g = ctx.gfx;
    g.clear(th::bg);
    g.set_font(ctx.font, th::sz_body);

    ui::Input in{ctx.input.mouse_x, ctx.input.mouse_y,
                 ctx.input.down(platform::MouseButton::Left),
                 ctx.input.pressed(platform::MouseButton::Left),
                 ctx.input.released(platform::MouseButton::Left)};
    ui_.begin(&g, in);
    const ui::Rect area{th::space_xl, th::space_xl,
                        g.width() - th::space_xl * 2, g.height() - th::space_xl * 2};
    pending_ = draw_hub_panel(ui_, g, view_ ? &*view_ : nullptr, path_, area, flash_, flash_t_);
    ui_.end();
}

} // namespace hubui
