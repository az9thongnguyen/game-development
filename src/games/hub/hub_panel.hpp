// =============================================================================
//  games/hub/hub_panel.hpp  —  the Hub, drawn once and reused
// =============================================================================
//  engine::hub_lines is the one hub *text*, shared by --hub and the windows so a
//  terminal and a window can never disagree. This is the same idea one level up:
//  the one hub *panel*, shared by --hub-ui and the Studio shell's Hub tab.
//
//  It is a pure function of (view, layout) -> "what did the user ask for". It does
//  not perform anything and it owns no state: the confirmation, the reason text and
//  the operation all belong to the scene, which is what lets both scenes reuse it
//  and what keeps this file free of engine::release calls.
// =============================================================================
#pragma once

#include <string>

#include "engine/hub/hub.hpp"
#include "engine/ui/ui.hpp"

namespace gfx { class Renderer2D; }

namespace hubui {

// What the user asked for this frame.
enum class Op {
    None = 0,
    Publish,             // -> development
    PromotePreview,      // development -> preview
    PromoteProduction,   // preview -> production
    Refresh,
    CopySourceHash,
};

// Draw one project's hub inside `area` and return what was clicked. `view` may be
// null (project unreadable), which draws the error state.
Op draw_hub_panel(ui::Context& ui, gfx::Renderer2D& g,
                  const engine::HubView* view, const std::string& project_path,
                  ui::Rect area);

// Human wording for a confirmation dialog about `op`, so the two scenes cannot
// describe the same irreversible action differently.
const char* op_title(Op op);
const char* op_body(Op op);
const char* op_verb(Op op);      // the accept button's label
bool        op_is_destructive(Op op);

} // namespace hubui
