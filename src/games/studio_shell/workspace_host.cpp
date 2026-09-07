// =============================================================================
//  games/studio_shell/workspace_host.cpp
// =============================================================================
#include "games/studio_shell/workspace_host.hpp"

#include <algorithm>
#include <utility>

#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"
#include "engine/ui/ui_input.hpp"

namespace studioshell {

namespace th = ui::theme;

WorkspaceHost::WorkspaceHost(std::unique_ptr<Workspace> ws) : ws_(std::move(ws)) {
    layout_ = read_layout();
    ws_->register_commands();
    recovery_ = ws_->recovery_pending();
}

void WorkspaceHost::update(double dt, const platform::InputState& in) {
    if (flash_t_ > 0) flash_t_ -= dt;
    if (layout_dirty_ && !in.down(platform::MouseButton::Left)) {
        layout_dirty_ = false;
        write_layout(layout_);
    }
    // A modal owns the input: the workspace still ticks (its autosave timer must not
    // stop while a dialog is up) but a click behind the card must not edit anything.
    ws_->update(dt, in, /*interactive*/ !recovery_);
    if (auto m = ws_->take_message()) { flash_ = m->message; flash_ok_ = m->ok; flash_t_ = 4.0; }
    // The workspace is deaf on purpose (it compiles into headless tests); the host
    // owns the device. Same two lines in the Studio shell, because a lab and a tab are
    // the same workspace object and an effect audible in only one of them is drift.
    for (const Workspace::SoundRequest& s : ws_->take_sounds()) sound_.play(s);
    sound_.pump();
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
    const ui::Rect body{pad, pad, w - pad * 2, h - pad * 2 - status_h};

    const int stored = layout_.width_for(ws_->name(), ws_->inspector_width());
    const int insp_w = fit_inspector(stored, body.w);
    const int handle = th::space_md;
    const int hx     = body.x + body.w - insp_w - handle;
    int       pos    = hx;
    const int most  = body.w / 2;
    const int least = std::min(kMinInspector, most);
    if (ui_.splitter("wssplit", ui::Rect{hx, body.y, handle, body.h}, pos,
                     body.x + body.w - most - handle,
                     body.x + body.w - least - handle)) {
        layout_.set(ws_->name(), body.x + body.w - pos - handle);
        layout_dirty_ = true;
    }

    ws_->draw_canvas(ui_, g, ui::Rect{body.x, body.y, body.w - insp_w - handle, body.h});
    ws_->draw_inspector(ui_, g, ui::Rect{body.x + body.w - insp_w, body.y, insp_w, body.h});

    ui_.status_bar(ui::Rect{pad, h - pad - th::space_xs, body.w, status_h},
                   ws_->status(), ws_->hint());

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
    // The workspace is deaf on purpose (it compiles into headless tests); the host
    // owns the device. Same two lines in the Studio shell, because a lab and a tab are
    // the same workspace object and an effect audible in only one of them is drift.
    for (const Workspace::SoundRequest& s : ws_->take_sounds()) sound_.play(s);
    sound_.pump();
    }

    if (flash_t_ > 0 && !flash_.empty())
        ui_.toast(flash_.c_str(), flash_ok_ ? ui::Tone::Success : ui::Tone::Danger);

    ui_.end();
}

} // namespace studioshell
