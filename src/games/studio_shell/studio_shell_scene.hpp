// =============================================================================
//  games/studio_shell/studio_shell_scene.hpp  —  the Studio shell (--shell)
// =============================================================================
//  Horizon 1's "shared Studio shell and dock/navigation model": a window with a left
//  nav rail (Hub / Guide / Learn / About) and a main panel that renders the selected
//  section. The Hub section is not a second implementation of the hub — it draws the
//  same hubui::draw_hub_panel that --hub-ui draws, over the same engine::hub_lines
//  content and the same engine::release ops that --hub prints and the CLI runs.
//
//  This scene is the frame and the navigation; every panel's substance lives in a
//  tested core, and every colour comes from ui::theme.
//  ponytail: four sections and the shell that hosts them. A dock manager, thumbnails,
//  and folding the Labs in come when a second author actually needs them.
// =============================================================================
#pragma once
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "engine/hub/hub.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "games/hub/hub_panel.hpp"
#include "games/studio_shell/map_workspace.hpp"
#include "engine/asset/provenance.hpp"
#include "engine/project/inspect.hpp"
#include "games/studio_shell/palette.hpp"
#include "games/studio_shell/play_viewport.hpp"
#include "games/studio_shell/project_panel.hpp"
#include "games/studio_shell/pixel_workspace.hpp"
#include "games/studio_shell/scene_workspace.hpp"
#include "games/studio_shell/sound_bank.hpp"
#include "games/studio_shell/workspace.hpp"

namespace studioshell {

class StudioShellScene : public engine::Scene {
public:
    // known_entries is INJECTED, not built here. The scene used to hold its own
    // {"fps"} literal while main.cpp knew {"fps","farm"}, so the Studio reported the
    // farm project as having an unknown entry while --project-inspect said OK. One
    // list, owned by the thing that can actually launch an entry.
    StudioShellScene(std::string project_path, std::vector<std::string> known_entries);
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

    // Clipboard access is INJECTED rather than called. The scene must stay linkable
    // without the SDL backend — that is what lets test_shell_golden drive the whole
    // shell headless — so main.cpp wires these to platform::clipboard_* and a test
    // leaves them unset (copying then does nothing, which is the truth).
    void set_clipboard(std::function<std::string()> get,
                       std::function<void(const std::string&)> set);

    // The project as this scene last read it — the same engine::inspect answer the
    // CLI prints. Exposed so a test can assert the Studio and the CLI agree.
    [[nodiscard]] const engine::Inspection& inspection() const { return inspection_; }

    // How a game gets built for the Play viewport. INJECTED, like the clipboard, so
    // this scene stays linkable without SDL and main.cpp keeps the one entry table.
    void set_play_factory(PlayViewport::Factory f) { play_.set_factory(std::move(f)); }
    [[nodiscard]] const PlayViewport& play() const { return play_; }
    [[nodiscard]] PlayViewport&       play() { return play_; }

    // The workspaces, for tests and for the status bar.
    [[nodiscard]] const MapWorkspace& map_workspace() const { return map_; }
    [[nodiscard]] MapWorkspace&       map_workspace() { return map_; }
    [[nodiscard]] const SceneWorkspace& scene_workspace() const { return scene_; }
    [[nodiscard]] SceneWorkspace&       scene_workspace() { return scene_; }
    [[nodiscard]] const PixelWorkspace& pixel_workspace() const { return pixels_; }
    [[nodiscard]] PixelWorkspace&       pixel_workspace() { return pixels_; }
    [[nodiscard]] int open_workspace() const { return ws_; }

private:
    // Map first: this is an authoring tool, and the thing you came to do should be
    // the thing that is already open.
    enum Section { Edit = 0, Project, Play, Hub, Guide, Learn, About, SectionCount };
    // One modal at a time, by construction. Two booleans would eventually both be
    // true and draw two cards on top of each other.
    enum class Modal { None, HubOp, Recovery };

    // The first `asset map` the manifest declares, or empty. Static so it can run in
    // the member-init list, before the object exists.
    // The first asset of a given type the manifest declares, or empty. Static so it
    // can run in the member-init list, before the object exists.
    static std::string asset_of(const std::string& project_path,
                                const std::vector<std::string>& known_entries,
                                const char* type);
    // ...and every asset of that type, for the workspace that edits one of several.
    static std::vector<std::string> assets_of(const std::string& project_path,
                                              const std::vector<std::string>& known_entries,
                                              const char* type);

    // One refresh: the hub view and the inspection are two readings of the same
    // files, and letting them go stale independently is how a panel ends up
    // disagreeing with the panel next to it.
    void rebuild();
    void run(hubui::Op op);
    void flash(const engine::OpResult& r, double seconds = 5.0);
    void draw_edit_section(gfx::Renderer2D& g, ui::Rect area);
    void draw_play_section(gfx::Renderer2D& g, ui::Rect area, text::Font* font, double dt);
    void run(projectui::Op op);

    std::string                    project_path_;
    std::vector<std::string>       known_entries_;
    int                            section_ = Edit;  // an authoring tool opens on the work
    std::optional<engine::HubView> hub_;
    engine::Inspection             inspection_{};
    std::vector<engine::AuditRecord> history_;   // read with the rest, drawn newest-first
    int                            asset_sel_ = 0;
    std::string                    flash_;
    bool                           flash_ok_ = true;
    double                         flash_t_ = 0;
    ui::Context                    ui_;
    SoundBank                      sound_;
    hubui::Op                      requested_  = hubui::Op::None;   // asked this frame
    hubui::Op                      confirming_ = hubui::Op::None;   // pending hub op
    Modal                          modal_ = Modal::None;
    std::string                    reason_;
    int                            nav_click_ = -1;
    MapWorkspace                   map_;
    SceneWorkspace                 scene_;
    PixelWorkspace                 pixels_;
    engine::Ledger                 ledger_{};   // where each picture came from
    // Concrete members, plus a vector of pointers to drive them through the interface.
    // ponytail: the set is fixed at construction, so no allocation and no ownership
    // question; it becomes unique_ptrs the day a workspace can be opened and closed.
    std::vector<Workspace*>        workspaces_;
    int                            ws_ = 0;          // the open tab
    int                            ws_click_ = -1;   // a tab clicked during the last draw
    Workspace*                     recovering_ = nullptr;   // whose autosave is being offered
    CommandPalette                 palette_;
    PlayViewport                   play_;
    bool                           play_focused_ = false;   // the game has the keyboard
    bool                           play_focus_click_ = false;   // clicked during the last draw
    int                            play_button_ = -1;           // toolbar button clicked
    std::string                    palette_click_;   // id clicked during the last draw
    std::function<std::string()>            clip_get_;
    std::function<void(const std::string&)> clip_set_;
};

} // namespace studioshell
