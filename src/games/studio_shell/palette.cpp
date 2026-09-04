// =============================================================================
//  games/studio_shell/palette.cpp
// =============================================================================
#include "games/studio_shell/palette.hpp"

#include <algorithm>

#include "engine/commands/registry.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"

namespace studioshell {

namespace th = ui::theme;

namespace {
constexpr int kMaxRows = 8;      // taller than this and the card fights the screen
constexpr int kRowH    = 30;
}  // namespace

void CommandPalette::open() {
    open_ = true;
    just_opened_ = true;
    query_.clear();
    sel_ = 0;
}

void CommandPalette::close() { open_ = false; }

void CommandPalette::move(int delta) {
    const int n = static_cast<int>(cmd::filter(query_).size());
    if (n == 0) { sel_ = 0; return; }
    // Wrap: with eight visible rows, walking off the bottom to reach the top is the
    // shorter path more often than not.
    sel_ = ((sel_ + delta) % n + n) % n;
}

std::string CommandPalette::selected() const {
    const auto hits = cmd::filter(query_);
    if (hits.empty()) return {};
    const int i = std::clamp(sel_, 0, static_cast<int>(hits.size()) - 1);
    return cmd::all()[hits[static_cast<std::size_t>(i)]].id;
}

std::string CommandPalette::draw(ui::Context& ui, gfx::Renderer2D& g) {
    if (!open_) return {};

    // The card's controls are live even though the screen behind it is inert — the
    // same contract confirm() has, since both own the screen while they are up.
    const bool was_inert = ui.set_inert(false);
    ui.push_id("palette");

    const auto hits = cmd::filter(query_);
    const int rows  = std::min(static_cast<int>(hits.size()), kMaxRows);
    const int w = 560;
    const int h = th::space_lg * 2 + 34 + th::space_sm + rows * (kRowH + 2) + th::space_lg;
    const int x = (g.width() - w) / 2;
    const int y = std::max(th::space_xl, g.height() / 6);

    g.fill_rect_blend(0, 0, g.width(), g.height(), th::scrim);
    g.drop_shadow(x, y, w, h, th::radius_md, th::shadow_panel.dx, th::shadow_panel.dy,
                  th::shadow_panel.spread, gfx::rgba(0, 0, 0, th::shadow_panel.a));
    g.fill_round_rect(x, y, w, h, th::radius_md, th::elevated);
    g.draw_round_rect(x, y, w, h, th::radius_md, th::border_strong);

    // The query field takes the keyboard the moment the palette opens; a palette you
    // have to click into first is slower than the menu it replaces.
    const ui::Rect q{x + th::space_lg, y + th::space_lg, w - th::space_lg * 2, 34};
    if (just_opened_) { ui.set_focus(ui.id_for("query")); just_opened_ = false; }
    if (ui.text_input("query", q, query_, "Type a command...")) {
        sel_ = 0;   // the list moved under the selection; anything else selects at random
    }

    std::string chosen;
    int ry = q.y + q.h + th::space_sm;
    if (hits.empty()) {
        g.set_font_size(th::sz_body);
        g.draw_text(q.x, ry + th::space_xs, "no command matches", th::text_muted);
    }
    // Scroll the window of visible rows to keep the selection inside it, rather than
    // clamping the selection to the first eight commands.
    const int first = std::clamp(sel_ - kMaxRows + 1, 0,
                                 std::max(0, static_cast<int>(hits.size()) - kMaxRows));
    for (int i = first; i < static_cast<int>(hits.size()) && i < first + kMaxRows; ++i) {
        const cmd::Info& info = cmd::all()[hits[static_cast<std::size_t>(i)]];
        ui.push_id(i);
        const bool needs_args = !info.args_help.empty();
        const char* right = !info.hotkey.empty() ? info.hotkey.c_str()
                          : needs_args           ? info.args_help.c_str()
                                                 : nullptr;
        if (ui.list_item(ui::Rect{q.x, ry, q.w, kRowH}, info.title.c_str(), i == sel_,
                         info.id.c_str(), right,
                         needs_args ? ui::Tone::Warning : ui::Tone::Neutral))
            chosen = info.id;
        ui.pop_id();
        ry += kRowH + 2;
    }

    ui.pop_id();
    ui.set_inert(was_inert);
    return chosen;
}

} // namespace studioshell
