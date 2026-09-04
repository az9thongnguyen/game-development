// =============================================================================
//  games/hub/hub_panel.cpp  —  the shared Hub panel
// =============================================================================
#include "games/hub/hub_panel.hpp"

#include <cstdio>

#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"

namespace hubui {
namespace {

namespace th = ui::theme;

// Release ids are 16 hex characters; only the first 8 carry recognition value at a
// glance. The ellipsis is a real U+2026 — before the UTF-8 fix it printed as three
// question marks, which is what started this whole slice.
std::string short_hash(const std::string& h) {
    return h.size() > 8 ? h.substr(0, 8) + "…" : h;
}

struct Status { const char* word; ui::Tone tone; };

Status channel_status(const engine::HubChannel& c) {
    if (c.release.empty()) return {"unset",   ui::Tone::Neutral};
    if (!c.present)        return {"MISSING", ui::Tone::Danger};
    if (c.matches_local)   return {"in sync", ui::Tone::Success};
    return {"behind", ui::Tone::Warning};
}

// One channel per column, in pipeline order, so the eye reads left to right in the
// direction a release actually travels.
gfx::Color channel_tone(const std::string& name) {
    if (name == "development") return th::chan_dev;
    if (name == "preview")     return th::chan_preview;
    return th::chan_prod;                                 // production
}

void card(gfx::Renderer2D& g, ui::Rect r) {
    g.fill_round_rect(r.x, r.y, r.w, r.h, th::radius_md, th::elevated);
    g.draw_round_rect(r.x, r.y, r.w, r.h, th::radius_md, th::border);
}

} // namespace

const char* op_title(Op op) {
    switch (op) {
        case Op::Publish:           return "Publish to development?";
        case Op::PromotePreview:    return "Promote to preview?";
        case Op::PromoteProduction: return "Promote to production?";
        default:                    return "";
    }
}

const char* op_body(Op op) {
    switch (op) {
        case Op::Publish:
            return "Writes an immutable release and points development at it.";
        case Op::PromotePreview:
            return "Moves the preview channel to the development release.";
        case Op::PromoteProduction:
            return "This is what players get. It takes effect immediately.";
        default: return "";
    }
}

const char* op_verb(Op op) {
    switch (op) {
        case Op::Publish:           return "Publish";
        case Op::PromotePreview:    return "Promote";
        case Op::PromoteProduction: return "Promote";
        default:                    return "OK";
    }
}

// Only production is treated as destructive. Publishing is additive and promoting to
// preview is reversible in one step; changing what players are running is neither.
bool op_is_destructive(Op op) { return op == Op::PromoteProduction; }

Op draw_hub_panel(ui::Context& ui, gfx::Renderer2D& g,
                  const engine::HubView* view, const std::string& project_path,
                  ui::Rect area) {
    Op op = Op::None;

    if (!view) {
        g.set_font_size(th::sz_title);
        g.draw_text(area.x, area.y, "Cannot read this project", th::danger);
        g.set_font_size(th::sz_body);
        g.draw_text(area.x, area.y + th::sz_title + th::space_sm, project_path.c_str(), th::text_dim);
        return op;
    }

    ui.begin_layout(area, ui::Axis::Y, ui::LayoutOpts{th::space_md, 0});

    // ---- heading -------------------------------------------------------------
    {
        const ui::Rect r = ui.slot(th::sz_display);
        g.set_font_size(th::sz_display);
        g.draw_text(r.x, r.y, view->name.c_str(), th::text);
    }
    {
        const ui::Rect r = ui.slot(th::sz_body + th::space_xs);
        char sub[200];
        std::snprintf(sub, sizeof sub, "entry %s   ·   schema %d   ·   %s",
                      view->entry.c_str(), view->schema, project_path.c_str());
        g.set_font_size(th::sz_body);
        g.draw_text(r.x, r.y, sub, th::text_dim);
        ui.badge(r.x + g.text_width(sub) + th::space_md, r.y - 2,
                 view->shippable ? "shippable" : "NOT shippable",
                 view->shippable ? ui::Tone::Success : ui::Tone::Danger, th::bg);
    }

    // ---- problems: the reason nothing else here can proceed ------------------
    if (!view->problems.empty()) {
        const int n = static_cast<int>(view->problems.size());
        const ui::Rect r = ui.slot(th::space_md * 2 + n * (th::sz_body + th::space_xs));
        card(g, r);
        int py = r.y + th::space_md;
        for (const auto& p : view->problems) {
            g.set_font_size(th::sz_body);
            g.draw_text(r.x + th::space_md, py, ("•  " + p).c_str(), th::warn);
            py += th::sz_body + th::space_xs;
        }
    }

    // ---- source: the bytes every channel is compared against -----------------
    {
        const ui::Rect r = ui.slot(th::space_md * 2 + th::sz_caption + th::space_xs + th::sz_label);
        card(g, r);
        g.set_font_size(th::sz_caption);
        g.draw_text(r.x + th::space_md, r.y + th::space_md, "SOURCE PACKAGE", th::text_muted);
        g.set_font_size(th::sz_label);
        const bool have = !view->local_package.empty();
        g.draw_text(r.x + th::space_md, r.y + th::space_md + th::sz_caption + th::space_xs,
                    have ? view->local_package.c_str() : "—", have ? th::text : th::text_muted);
        if (have) {
            const ui::Rect b{r.x + r.w - th::space_md - 70, r.y + (r.h - 26) / 2, 70, 26};
            if (ui.button(b, "Copy")) op = Op::CopySourceHash;
            ui.tooltip(b, "Copy the full 16-character release id");
        }
    }

    // ---- the three channels, as cards in pipeline order ----------------------
    const int ch_h = th::space_md * 2 + th::sz_label + th::space_sm + th::sz_body +
                     th::space_sm + th::sz_caption;
    {
        const ui::Rect row = ui.slot(ch_h);
        ui.begin_layout(row, ui::Axis::X, ui::LayoutOpts{th::space_md, 0});
        const int n = static_cast<int>(view->channels.size());
        for (int i = 0; i < n; ++i) {
            const engine::HubChannel& c = view->channels[static_cast<std::size_t>(i)];
            const ui::Rect cr = ui.cell(n, i);
            card(g, cr);
            // A 3px rail in the channel's colour: the card is identifiable before a
            // single word on it is read.
            g.fill_round_rect(cr.x, cr.y, 3, cr.h, 0, channel_tone(c.name));

            g.set_font_size(th::sz_label);
            g.draw_text(cr.x + th::space_md, cr.y + th::space_md, c.name.c_str(), th::text);

            const Status st = channel_status(c);
            g.set_font_size(th::sz_caption);
            const int bw = g.text_width(st.word) + th::space_md;
            ui.badge(cr.x + cr.w - th::space_md - bw, cr.y + th::space_md, st.word, st.tone);

            g.set_font_size(th::sz_body);
            g.draw_text(cr.x + th::space_md, cr.y + th::space_md + th::sz_label + th::space_sm,
                        c.release.empty() ? "not published" : short_hash(c.release).c_str(),
                        c.release.empty() ? th::text_muted : th::text_dim);
            if (!c.release.empty()) {
                g.set_font_size(th::sz_caption);
                g.draw_text(cr.x + th::space_md,
                            cr.y + th::space_md + th::sz_label + th::space_sm + th::sz_body + th::space_sm,
                            c.matches_local ? "matches your source" : "differs from your source",
                            th::text_muted);
                ui.tooltip(cr, c.release.c_str());
            }
        }
        ui.end_layout();
    }

    // ---- the one recommended step, then the controls that perform it ---------
    const engine::Next next = engine::next_action(*view);
    {
        const ui::Rect r = ui.slot(th::sz_label);
        g.set_font_size(th::sz_label);
        g.draw_text(r.x, r.y, engine::recommend(*view).c_str(),
                    next == engine::Next::Fix ? th::warn
                    : next == engine::Next::InSync ? th::success : th::accent);
    }
    {
        // Exactly one primary button per screen — whichever step next_action names.
        // Everything else stays neutral, so the accent still means "do this".
        const ui::Rect row = ui.slot(30);
        ui.begin_layout(row, ui::Axis::X, ui::LayoutOpts{th::space_sm, 0});
        const int refresh_w = 90;
        // Widths follow the space available: the window resizes, and four fixed-width
        // buttons ran off the right edge below about 1000px.
        const int avail = row.w - refresh_w - th::space_sm * 3;
        const int bw = avail / 3 < 120 ? 120 : (avail / 3 > 210 ? 210 : avail / 3);
        const bool ok = view->shippable;
        if (ui.button(ui.slot(bw), "Publish → development",
                      next == engine::Next::Publish, ok)) op = Op::Publish;
        if (ui.button(ui.slot(bw), "Promote → preview",
                      next == engine::Next::PromotePreview, ok)) op = Op::PromotePreview;
        if (ui.button(ui.slot(bw), "Promote → production",
                      next == engine::Next::PromoteProduction, ok)) op = Op::PromoteProduction;
        if (ui.button(ui.slot_end(refresh_w), "Refresh")) op = Op::Refresh;
        ui.end_layout();
    }

    {
        const ui::Rect r = ui.slot_end(th::sz_caption);
        g.set_font_size(th::sz_caption);
        g.draw_text(r.x, r.y, "Space publishes  ·  1 and 2 promote  ·  R refreshes  ·  Tab moves focus",
                    th::text_muted);
    }

    ui.end_layout();
    return op;
}

} // namespace hubui
