// =============================================================================
//  games/studio_shell/workspace_host.hpp  —  one workspace, full screen
// =============================================================================
//  A Scene that runs a single Workspace over the whole window: canvas, inspector,
//  status strip, autosave-recovery prompt, toast. It is what `--sandbox` is now.
//
//  The point is that there is no second editor. The sandbox used to be a Scene with
//  its own palette, its own selection handling and its own save keys, none of which
//  the Studio could reach; folding it into a Workspace and giving that workspace two
//  frames — a tab in the Studio and this window — means the two cannot drift, and the
//  features only one of them had (undo, autosave, the command palette) now belong to
//  both.
// =============================================================================
#pragma once

#include <memory>
#include <string>

#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "games/studio_shell/layout.hpp"
#include "games/studio_shell/sound_bank.hpp"
#include "games/studio_shell/workspace.hpp"

namespace studioshell {

class WorkspaceHost : public engine::Scene {
public:
    explicit WorkspaceHost(std::unique_ptr<Workspace> ws);

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;
    [[nodiscard]] ui::CursorHint cursor_hint() const override {
        return ui_.cursor_hint();
    }

    [[nodiscard]] Workspace& workspace() { return *ws_; }

private:
    std::unique_ptr<Workspace> ws_;
    ui::Context                ui_;
    // The SAME file the Studio's Edit tab reads. A divider dragged in the lab and
    // forgotten in the tab would be exactly the drift this class exists to prevent.
    Layout                     layout_{};
    bool                       layout_dirty_ = false;
    SoundBank                  sound_;
    bool                       recovery_ = false;
    std::string                flash_;
    bool                       flash_ok_ = true;
    double                     flash_t_ = 0;
};

} // namespace studioshell
