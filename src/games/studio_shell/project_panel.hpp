// =============================================================================
//  games/studio_shell/project_panel.hpp  —  what this project is made of
// =============================================================================
//  The asset browser and the validation panel, over engine::Inspection. Same shape
//  as hub_panel: a pure function of (data, layout) -> "what did the user ask for".
//  It performs nothing and owns no state — even the selected row lives in the caller,
//  because the caller is what survives between frames.
//
//  The point of this panel is that it shows the SAME verdict `--project-inspect`
//  prints, from the same engine::inspect call. A Studio that computed its own answer
//  would eventually disagree with the CLI, and the operator would have no way to know
//  which one was lying.
// =============================================================================
#pragma once

#include "engine/project/inspect.hpp"
#include "engine/ui/ui.hpp"

namespace gfx { class Renderer2D; }

namespace projectui {

enum class Op {
    None = 0,
    Reinspect,          // re-read the manifest and re-hash every asset from disk
    CopyPackageHash,    // the release id this source would publish as
};

// Draw one project's content and verdict inside `area`; returns what was clicked.
// `selected` is an index into in.assets, clamped by the panel (the asset list can
// shrink between inspections, and a stale index would read off the end).
Op draw_project_panel(ui::Context& ui, gfx::Renderer2D& g,
                      const engine::Inspection& in, ui::Rect area, int& selected);

} // namespace projectui
