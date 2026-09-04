// =============================================================================
//  games/hub/hub_panel.hpp  —  the Hub, drawn once and reused
// =============================================================================
//  engine::hub_lines is the one hub *text*, shared by --hub and the windows so a
//  terminal and a window can never disagree. This is the same idea one level up:
//  the one hub *panel*, shared by --hub-ui and the Studio shell's Hub tab, which
//  until now each carried their own copy of the layout, the colours, the keyboard
//  shortcuts and the footer string (including the same broken arrow, twice).
//
//  Draws through ui::Context and ui::theme — no colour literals — so the Hub reads
//  as part of the same designed system as every other scene.
// =============================================================================
#pragma once

#include <string>

#include "engine/hub/hub.hpp"
#include "engine/ui/ui.hpp"

namespace gfx { class Renderer2D; }

namespace hubui {

// What the user asked for this frame. At most one is true.
struct Action {
    bool publish            = false;
    bool promote_preview    = false;
    bool promote_production = false;
    bool refresh            = false;
};

// Draw one project's hub inside `area` and return the action the user clicked.
// `view` may be null (project unreadable) — that draws the error state.
// `flash`/`flash_t` are the last operation's message and its remaining seconds.
Action draw_hub_panel(ui::Context& ui, gfx::Renderer2D& g,
                      const engine::HubView* view, const std::string& project_path,
                      ui::Rect area, const std::string& flash, double flash_t);

} // namespace hubui
