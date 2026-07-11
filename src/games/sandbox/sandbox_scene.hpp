// =============================================================================
//  games/sandbox/sandbox_scene.hpp  —  the declarative sandbox (--sandbox)
// =============================================================================
//  Edit mode: pick an archetype from the palette, click to place it, select/drag
//  actors, tweak them in the inspector. Play runs the deterministic World::tick
//  (in update(), fixed step); Stop restores the snapshot taken at Play. F5/F9
//  save/load. This scene is the ONLY sandbox file that touches UI/renderer/input.
// =============================================================================
#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "engine/image.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "games/sandbox/world.hpp"

namespace sandbox {

class SandboxScene : public engine::Scene {
public:
    SandboxScene();
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    // A palette entry = an Archetype plus which proto-carrying behavior to attach.
    enum class Extra { None, Emitter, Sweeper };
    struct PaletteItem { const char* label; Archetype arch; Extra extra = Extra::None; };

    void place(int palette_index, float x, float y);   // spawn + attach Extra
    bool pick_at(float x, float y, ecs::Entity& out) const;
    void toggle_play();
    void save() const;
    void load();
    void load_textures();                              // probe the Texture Lab collection

    World                    world_;
    std::vector<PaletteItem> palette_;
    ui::Context              ui_;

    int         armed_ = -1;            // palette index armed for placing; -1 = select/move
    ecs::Entity sel_{};
    bool        has_sel_  = false;
    bool        dragging_ = false;
    float       drag_dx_ = 0, drag_dy_ = 0;
    bool        playing_  = false;
    std::string snapshot_;              // scene text captured on Play, restored on Stop
    bool        inited_   = false;
    int         color_idx_ = 0;
    int         w_ = 0, h_ = 0;

    // Textures discovered from the Texture Lab collection (name -> decoded image).
    std::unordered_map<std::string, gfx::Image> tex_;
    std::vector<std::string>                    tex_names_;
};

} // namespace sandbox
