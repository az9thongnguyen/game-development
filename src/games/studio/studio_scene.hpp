// =============================================================================
//  games/studio/studio_scene.hpp  —  the Texture Lab (--studio)
// =============================================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"
#include "games/studio/texture_gen.hpp"

namespace studio {

class StudioScene : public engine::Scene {
public:
    StudioScene();
    void render(const engine::Context& ctx) override;

private:
    void sync_params();                          // mirror slider/cycle state -> params_
    void regenerate();                           // params_ -> preview_
    void save_current();                         // encode + write .hrt + .recipe
    void load_saved(const std::string& name);    // read .recipe -> params_

    TextureParams            params_;
    gfx::Image               preview_;
    bool                     dirty_ = true;
    ui::Context              ui_;

    // slider/cycle mirror state
    float freq_f_ = 4, oct_f_ = 4, gain_f_ = 0.5f, lac_f_ = 2, opamt_f_ = 0.5f;
    int   base_idx_ = 0, op_idx_ = 0, ramp_idx_ = 0;
    std::uint32_t seed_ = 1;

    int save_counter_ = 0;
    std::vector<std::string> collection_;        // saved texture names (this session)
    int w_ = 0, h_ = 0;
};

} // namespace studio
