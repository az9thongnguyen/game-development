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

// Blend two opaque colours. fill_rect writes opaquely (alpha is only honoured by the
// per-pixel blend paths), so a "tinted" badge background is produced by mixing here rather
// than by drawing a translucent rectangle.
// ponytail: when S2 adds a real alpha fill for the modal scrim, this can go.
gfx::Color mix(gfx::Color fg, gfx::Color bg, int pct) {
    const auto ch = [&](int shift) {
        const int a = static_cast<int>((fg >> shift) & 0xFF);
        const int b = static_cast<int>((bg >> shift) & 0xFF);
        return static_cast<std::uint32_t>((a * pct + b * (100 - pct)) / 100) << shift;
    };
    return 0xFF000000u | ch(16) | ch(8) | ch(0);
}

// A pill: tinted background, coloured text. The status of a thing should be visible
// as colour AND readable as a word — colour alone excludes anyone who cannot
// distinguish these hues, and a word alone makes the eye read every row.
void badge(gfx::Renderer2D& g, int x, int y, const char* label, gfx::Color tone, gfx::Color on) {
    g.set_font_size(th::sz_caption);
    const int w = g.text_width(label) + th::space_md;
    const int h = th::sz_caption + th::space_sm;
    g.fill_round_rect(x, y, w, h, th::radius_sm, mix(tone, on, 22));
    g.draw_text(x + th::space_sm - 2, y + th::space_xs - 1, label, tone);
}

int badge_width(gfx::Renderer2D& g, const char* label) {
    g.set_font_size(th::sz_caption);
    return g.text_width(label) + th::space_md;
}

// Release ids are 16 hex characters; only the first 8 carry any recognition value
// at a glance. The ellipsis is a real U+2026 — before the UTF-8 fix it would have
// printed as three question marks, which is exactly the bug this slice removes.
std::string short_hash(const std::string& h) {
    return h.size() > 8 ? h.substr(0, 8) + "…" : h;
}

struct Status { const char* word; gfx::Color tone; };

Status channel_status(const engine::HubChannel& c) {
    if (c.release.empty()) return {"unset",   th::text_muted};
    if (!c.present)        return {"MISSING", th::danger};
    if (c.matches_local)   return {"in sync", th::success};
    return {"behind", th::warn};
}

// One channel per column, in pipeline order, so the eye reads left-to-right the
// same direction the release actually travels.
gfx::Color channel_tone(const std::string& name) {
    if (name == "development") return th::accent;
    if (name == "preview")     return th::warn;
    return th::success;                                   // production
}

void card(gfx::Renderer2D& g, ui::Rect r) {
    g.fill_round_rect(r.x, r.y, r.w, r.h, th::radius_md, th::elevated);
    g.draw_round_rect(r.x, r.y, r.w, r.h, th::radius_md, th::border);
}

} // namespace

Action draw_hub_panel(ui::Context& ui, gfx::Renderer2D& g,
                      const engine::HubView* view, const std::string& project_path,
                      ui::Rect area, const std::string& flash, double flash_t) {
    Action act;
    const int x = area.x;
    int       y = area.y;

    if (!view) {
        g.set_font_size(th::sz_title);
        g.draw_text(x, y, "Cannot read this project", th::danger);
        g.set_font_size(th::sz_body);
        g.draw_text(x, y + th::sz_title + th::space_sm, project_path.c_str(), th::text_dim);
        return act;
    }

    // ---- heading: what am I looking at ----
    g.set_font_size(th::sz_display);
    g.draw_text(x, y, view->name.c_str(), th::text);
    y += th::sz_display + th::space_xs;

    char sub[160];
    std::snprintf(sub, sizeof sub, "entry %s   ·   schema %d   ·   %s",
                  view->entry.c_str(), view->schema, project_path.c_str());
    g.set_font_size(th::sz_body);
    g.draw_text(x, y, sub, th::text_dim);
    badge(g, x + g.text_width(sub) + th::space_md, y - 2,
          view->shippable ? "shippable" : "NOT shippable",
          view->shippable ? th::success : th::danger, th::bg);
    y += th::sz_body + th::space_lg;

    // ---- problems, if any: the reason nothing else on this screen can proceed ----
    if (!view->problems.empty()) {
        const int ph = th::space_md * 2 + static_cast<int>(view->problems.size()) * (th::sz_body + th::space_xs);
        card(g, ui::Rect{x, y, area.w, ph});
        int py = y + th::space_md;
        for (const auto& p : view->problems) {
            g.set_font_size(th::sz_body);
            g.draw_text(x + th::space_md, py, ("•  " + p).c_str(), th::warn);
            py += th::sz_body + th::space_xs;
        }
        y += ph + th::space_md;
    }

    // ---- source card: the bytes every channel is compared against ----
    {
        const int h = th::space_md * 2 + th::sz_caption + th::space_xs + th::sz_label;
        card(g, ui::Rect{x, y, area.w, h});
        g.set_font_size(th::sz_caption);
        g.draw_text(x + th::space_md, y + th::space_md, "SOURCE PACKAGE", th::text_muted);
        g.set_font_size(th::sz_label);
        g.draw_text(x + th::space_md, y + th::space_md + th::sz_caption + th::space_xs,
                    view->local_package.empty() ? "—" : view->local_package.c_str(),
                    view->local_package.empty() ? th::text_muted : th::text);
        y += h + th::space_md;
    }

    // ---- the three channels, as cards in pipeline order ----
    {
        const int n    = static_cast<int>(view->channels.size());
        const int gap  = th::space_md;
        const int cw   = n > 0 ? (area.w - gap * (n - 1)) / n : area.w;
        const int ch_h = th::space_md * 2 + th::sz_label + th::space_sm + th::sz_body + th::space_sm + th::sz_caption + th::space_sm;
        for (int i = 0; i < n; ++i) {
            const engine::HubChannel& c = view->channels[static_cast<std::size_t>(i)];
            const int cx = x + i * (cw + gap);
            card(g, ui::Rect{cx, y, cw, ch_h});

            // A 3px rail in the channel's colour: the card is identifiable before
            // any word on it is read.
            g.fill_round_rect(cx, y, 3, ch_h, 0, channel_tone(c.name));

            g.set_font_size(th::sz_label);
            g.draw_text(cx + th::space_md, y + th::space_md, c.name.c_str(), th::text);

            const Status st = channel_status(c);
            badge(g, cx + cw - th::space_md - badge_width(g, st.word), y + th::space_md, st.word, st.tone, th::elevated);

            g.set_font_size(th::sz_body);
            g.draw_text(cx + th::space_md, y + th::space_md + th::sz_label + th::space_sm,
                        c.release.empty() ? "not published" : short_hash(c.release).c_str(),
                        c.release.empty() ? th::text_muted : th::text_dim);

            if (!c.release.empty()) {
                g.set_font_size(th::sz_caption);
                g.draw_text(cx + th::space_md,
                            y + th::space_md + th::sz_label + th::space_sm + th::sz_body + th::space_sm,
                            c.matches_local ? "matches your source" : "differs from your source",
                            th::text_muted);
            }
        }
        y += ch_h + th::space_lg;
    }

    // ---- the one recommended step, then the controls that perform it ----
    const engine::Next next = engine::next_action(*view);
    g.set_font_size(th::sz_label);
    g.draw_text(x, y, engine::recommend(*view).c_str(),
                next == engine::Next::Fix ? th::warn
                : next == engine::Next::InSync ? th::success : th::accent);
    y += th::sz_label + th::space_md;

    {
        // Exactly one primary button per screen — whichever step `next_action` names.
        // Everything else stays neutral, so the accent still means "do this".
        const int bw = 210, bh = 30, gap = th::space_sm;
        int bx = x;
        const bool ok = view->shippable;
        if (ui.button(ui::Rect{bx, y, bw, bh}, "Publish → development",
                      next == engine::Next::Publish, ok)) act.publish = true;
        bx += bw + gap;
        if (ui.button(ui::Rect{bx, y, bw, bh}, "Promote → preview",
                      next == engine::Next::PromotePreview, ok)) act.promote_preview = true;
        bx += bw + gap;
        if (ui.button(ui::Rect{bx, y, bw, bh}, "Promote → production",
                      next == engine::Next::PromoteProduction, ok)) act.promote_production = true;
        bx += bw + gap;
        if (ui.button(ui::Rect{bx, y, 90, bh}, "Refresh")) act.refresh = true;
        y += bh + th::space_md;
    }

    // ---- the last operation's result ----
    if (flash_t > 0 && !flash.empty()) {
        const int h = th::sz_body + th::space_md * 2;
        g.fill_round_rect(x, y, area.w, h, th::radius_sm, mix(th::success, th::bg, 14));
        g.set_font_size(th::sz_body);
        g.draw_text(x + th::space_md, y + th::space_md, flash.c_str(), th::success);
    }

    g.set_font_size(th::sz_caption);
    g.draw_text(x, area.y + area.h - th::sz_caption,
                "Space publishes  ·  1 and 2 promote  ·  R refreshes",
                th::text_muted);
    return act;
}

} // namespace hubui
