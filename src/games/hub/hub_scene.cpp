// =============================================================================
//  games/hub/hub_scene.cpp  —  window glue around the shared Hub panel
// =============================================================================
#include "games/hub/hub_scene.hpp"

#include <utility>

#include "engine/hub/hub_build.hpp"
#include "engine/release/ops.hpp"
#include "engine/ui/theme.hpp"
#include "engine/ui/ui_input.hpp"

namespace hubui {

HubScene::HubScene(std::string project_path)
    : path_(std::move(project_path)), known_entries_{"fps"} {
    rebuild();
}

void HubScene::set_clipboard(std::function<std::string()> get,
                             std::function<void(const std::string&)> set) {
    clip_get_ = std::move(get);
    clip_set_ = std::move(set);
}

void HubScene::rebuild() { view_ = engine::build_hub_view(path_, known_entries_); }

void HubScene::run(Op op) {
    auto did = [&](const engine::OpResult& r) {
        flash_ = r.message; flash_ok_ = r.ok; flash_t_ = 5.0; rebuild();
    };
    const std::string why = reason_.empty() ? std::string("hub") : reason_;
    switch (op) {
        case Op::Publish:           did(engine::publish(path_, "development", why, known_entries_)); break;
        case Op::PromotePreview:    did(engine::promote("development", "preview", why)); break;
        case Op::PromoteProduction: did(engine::promote("preview", "production", why)); break;
        case Op::Refresh:           rebuild(); break;   // pick up external edits
        case Op::CopySourceHash:
            if (view_ && !view_->local_package.empty() && clip_set_) {
                clip_set_(view_->local_package);
                flash_ = "copied " + view_->local_package; flash_ok_ = true; flash_t_ = 3.0;
            }
            break;
        case Op::None: break;
    }
}

void HubScene::update(double dt, const platform::InputState& in) {
    if (flash_t_ > 0) flash_t_ -= dt;

    // Buttons resolve during render (that is where the layout is known), so a click
    // arrives here on the following frame; keys are read directly. Both funnel into
    // the same place, so mouse and keyboard cannot become two ways of publishing.
    Op asked = requested_;
    requested_ = Op::None;

    if (confirming_ == Op::None && asked == Op::None) {
        if      (in.pressed(platform::Key::Space)) asked = Op::Publish;
        else if (in.pressed(platform::Key::Num1))  asked = Op::PromotePreview;
        else if (in.pressed(platform::Key::Num2))  asked = Op::PromoteProduction;
        else if (in.pressed(platform::Key::R))     asked = Op::Refresh;
    }

    // Refresh and copy change nothing that a channel points at, so they run straight
    // away. Everything else appends to the audit log and needs a reason first.
    if (asked == Op::Refresh || asked == Op::CopySourceHash) {
        run(asked);
    } else if (asked != Op::None && confirming_ == Op::None) {
        confirming_ = asked;
        reason_.clear();
    }
}

void HubScene::render(const engine::Context& ctx) {
    namespace th = ui::theme;
    gfx::Renderer2D& g = ctx.gfx;
    g.clear(th::bg);
    g.set_font(ctx.font, th::sz_body);

    ui_.set_clipboard(clip_get_, clip_set_);
    ui_.begin(&g, ui::from_platform(ctx.input));

    if (confirming_ != Op::None) ui_.begin_inert();

    const ui::Rect area{th::space_xl, th::space_xl,
                        g.width() - th::space_xl * 2, g.height() - th::space_xl * 2};
    const Op clicked = draw_hub_panel(ui_, g, view_ ? &*view_ : nullptr, path_, area);
    if (clicked != Op::None) requested_ = clicked;

    if (confirming_ != Op::None) {
        const ui::Confirm c = ui_.confirm("hubop", op_title(confirming_), op_body(confirming_),
                                          op_verb(confirming_), op_is_destructive(confirming_),
                                          &reason_);
        if      (c == ui::Confirm::Yes) { run(confirming_); confirming_ = Op::None; }
        else if (c == ui::Confirm::No)  { confirming_ = Op::None; reason_.clear(); }
    }

    if (flash_t_ > 0 && !flash_.empty())
        ui_.toast(flash_.c_str(), flash_ok_ ? ui::Tone::Success : ui::Tone::Danger);

    ui_.end();
}

} // namespace hubui
