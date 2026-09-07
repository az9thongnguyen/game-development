// =============================================================================
//  games/creatures/creatures_scene.hpp  —  the second game, on screen
// =============================================================================
//  Everything that decides anything lives in `creature_core`: the battle, the walk,
//  the encounter roll, the experience, the save format. This file draws it and reads
//  the pointer, and that split is what lets `test_creatures_scene` play the game with
//  no window — press east until something jumps out of the grass, fight it, and check
//  the party grew.
//
//  It follows the two rules the farm's scene earned the hard way:
//
//   * ONE layout (creatures/controls.hpp), read by the renderer and the hit test, so
//     a control cannot be drawn in one place and hit in another;
//   * every verb has an on-screen control, because a game whose only input is a
//     keyboard is a game a phone cannot play — and the phone is where the link goes.
// =============================================================================
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/scene.hpp"
#include "engine/tilemap/camera2d.hpp"
#include "engine/tilemap/map2.hpp"
#include "engine/tilemap/theme.hpp"
#include "engine/tilemap/tileset.hpp"
#include "games/creatures/controls.hpp"
#include "games/creatures/world.hpp"

namespace creature {

inline constexpr int kTile = 16;

class CreaturesScene : public engine::Scene {
public:
    CreaturesScene();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

    // ---- for tests: what loaded, and the game it is driving ----
    [[nodiscard]] bool ready() const { return ready_; }
    [[nodiscard]] const std::string& problem() const { return problem_; }
    [[nodiscard]] const World& world() const { return world_; }
    [[nodiscard]] const Dex&   dex()   const { return dex_; }
    [[nodiscard]] Mode mode() const;
    [[nodiscard]] const std::string& message() const { return message_; }
    // The layout the LAST render used, in framebuffer coordinates — so a test taps
    // where a finger taps rather than calling the function behind the button.
    [[nodiscard]] Layout controls() const { return layout(fb_w_, fb_h_, mode()); }
    [[nodiscard]] int fb_width()  const { return fb_w_; }
    [[nodiscard]] int fb_height() const { return fb_h_; }
    // Where the camera put the world, so a test can turn a tile into a screen point.
    [[nodiscard]] int origin_x() const { return org_x_; }
    [[nodiscard]] int origin_y() const { return org_y_; }

    void update_world(double dt, const platform::InputState& input);
    void write_tape();
    bool save_game();
    bool load_game();

private:
    void load();
    [[nodiscard]] const tilemap::Tileset& sheet_of(const std::string& name) const;
    [[nodiscard]] const gfx::Image* creature_image(int species) const;
    bool draw_tile(gfx::Renderer2D& g, const char* layer, std::int32_t id,
                   int x, int y, int px, int py) const;
    void say(std::string msg, double seconds = 2.5);
    void choose_cell(int cell);
    void narrate();

    void render_overworld(const engine::Context& ctx);
    void render_battle(const engine::Context& ctx);
    void render_controls(gfx::Renderer2D& g) const;

    Dex             dex_;
    tilemap::Map    map_;
    tilemap::Theme  theme_;
    std::map<std::string, tilemap::Tileset> tiles_;
    mutable std::map<int, gfx::Image>       sprites_;   // species id -> its .hrt
    tilemap::Camera2D cam_;
    World           world_;

    bool        ready_ = false;
    std::string problem_;
    std::string message_;
    double      message_t_ = 0;

    // Menu state. `Mode` is derived from the world's phase plus this, so there is one
    // answer to "what is on screen" and the renderer and the hit test share it.
    bool  in_moves_ = false;
    bool  in_party_ = false;

    double step_cool_ = 0;      // grid movement: one tile, then a wait
    int    fb_w_ = 640, fb_h_ = 360;
    int    org_x_ = 0, org_y_ = 0;
    bool   prev_keys_[8] = {};
};

} // namespace creature
