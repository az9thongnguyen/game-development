// =============================================================================
//  games/studio_shell/workspace_host.cpp
// =============================================================================
#include "games/studio_shell/workspace_host.hpp"

#include <utility>

#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"
#include "engine/ui/ui_input.hpp"

namespace studioshell {

namespace th = ui::theme;

WorkspaceHost::WorkspaceHost(std::unique_ptr<Workspace> ws) : ws_(std::move(ws)) {
    ws_->register_commands();
    recovery_ = ws_->recovery_pending();
}

void WorkspaceHost::update(double dt, const platform::InputState& in) {
    if (flash_t_ > 0) flash_t_ -= dt;
    // A modal owns the input: the workspace still ticks (its autosave timer must not
    // stop while a dialog is up) but a click behind the card must not edit anything.
    ws_->update(dt, in, /*interactive*/ !recovery_);
    if (auto m = ws_->take_message()) { flash_ = m->message; flash_ok_ = m->ok; flash_t_ = 4.0; }
}

void WorkspaceHost::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const int w = g.width(), h = g.height();
    g.clear(th::bg);
    g.set_font(ctx.font, th::sz_body);

    ui_.begin(&g, ui::from_platform(ctx.input));
    if (recovery_) ui_.begin_inert();

    const int pad = th::space_lg;
    const int status_h = th::sz_caption + th::space_md;
    int insp_w = ws_->inspector_width();
    if (insp_w > (w - pad * 2) / 2) insp_w = (w - pad * 2) / 2;

    const ui::Rect body{pad, pad, w - pad * 2, h - pad * 2 - status_h};
    ws_->draw_canvas(ui_, g, ui::Rect{body.x, body.y, body.w - insp_w - th::space_md, body.h});
    ws_->draw_inspector(ui_, g, ui::Rect{body.x + body.w - insp_w, body.y, insp_w, body.h});

    g.set_font_size(th::sz_caption);
    const int sy = h - pad + th::space_xs;
    const std::string left = ws_->status();
    g.draw_text(pad, sy, left.c_str(), ws_->dirty() ? th::warn : th::text_muted);
    const char* hint = ws_->hint();
    g.draw_text(w - pad - g.text_width(hint), sy, hint, th::text_muted);

    if (recovery_) {
        const ui::Confirm c = ui_.confirm(
            "recover", "Unsaved changes were found",
            ("An autosave of " + ws_->path() + " is newer than the file.").c_str(),
            "Recover", /*danger*/ false);
        // Cancel keeps the saved file AND leaves the autosave alone: declining by
        // reflex must not be the thing that destroys the work.
        if (c == ui::Confirm::Yes)     { ws_->take_recovery();    recovery_ = false; }
        else if (c == ui::Confirm::No) { ws_->dismiss_recovery(); recovery_ = false; }
        if (auto m = ws_->take_message()) { flash_ = m->message; flash_ok_ = m->ok; flash_t_ = 4.0; }
    }

    if (flash_t_ > 0 && !flash_.empty())
        ui_.toast(flash_.c_str(), flash_ok_ ? ui::Tone::Success : ui::Tone::Danger);

    ui_.end();
}

} // namespace studioshell
