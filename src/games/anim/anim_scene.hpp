// =============================================================================
//  games/anim/anim_scene.hpp  —  the sprite-sheet animation playground (--anim)
// =============================================================================
//  Loads a vertically-packed sprite sheet (sprites/spin_8.hrt) and plays it with
//  a Flipbook: the big current frame, the whole strip with the active frame
//  highlighted, and fps / loop / play controls. Because the sheet is packed
//  vertically, each frame is a CONTIGUOUS sub-image, so a plain gfx::Sprite view
//  into the pixels + the existing blit_scaled draws it — no new renderer code.
//  See docs/book/86-sprite-animation.md.
// =============================================================================
#pragma once
#include "engine/anim/flipbook.hpp"
#include "engine/image.hpp"
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"

namespace animdemo {

class AnimScene : public engine::Scene {
public:
    AnimScene();
    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    gfx::Sprite frame_sprite(int f) const;   // contiguous view of frame f in the sheet

    gfx::Image     sheet_;
    bool           have_sheet_ = false;
    int            frames_ = 8;
    int            fh_ = 48;                  // per-frame height (sheet.h / frames_)
    anim::Flipbook fb_;
    ui::Context    ui_;
    float          fps_     = 8.0f;
    bool           loop_    = true;
    bool           playing_ = true;
    int            w_ = 960, h_ = 600;
};

} // namespace animdemo
