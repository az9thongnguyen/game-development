// =============================================================================
//  games/farm/farm_scene.hpp  —  the farm, played
// =============================================================================
//  The consumer of farm_core, and the second game to reach the platform through a
//  manifest rather than a CLI flag: `--project projects/farm.gameproject` resolves
//  `entry farm` to this scene. That is the whole point of the entry seam — a new game
//  is a manifest and a scene, not another branch in main.cpp.
//
//  It also gives `tilemap::Camera2D` its first consumer: the farm is larger than the
//  window, so the view has to follow the player, and that is what the deadzone and
//  the framerate-independent smoothing were written for.
//
//  No SDL: a Scene sees only a platform::InputState and a Renderer2D, so this can be
//  rendered offscreen and checked like the Studio shell is.
// =============================================================================
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "engine/scene.hpp"
#include "engine/tilemap/camera2d.hpp"
#include "engine/tilemap/map2.hpp"
#include "games/farm/defs.hpp"
#include "games/farm/dialogue.hpp"
#include "games/farm/world.hpp"

namespace farm {

class FarmScene : public engine::Scene {
public:
    // `map_path` and the data files are asset-relative. Everything is loaded here so
    // a failure has one place to be reported from, and the scene still runs (drawing
    // the reason) rather than launching into a black window.
    FarmScene();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

    // For tests: what loaded, and the simulation it is driving.
    [[nodiscard]] bool         ready() const { return ready_; }
    [[nodiscard]] const std::string& problem() const { return problem_; }
    [[nodiscard]] const World& world() const { return world_; }

private:
    enum class Tab { Hoe = 0, Water, Seed, Harvest };

    void        load();
    void        interact();                 // the Z/Space key
    void        sleep_now(bool collapsed);
    std::string save_path() const;
    void        save_game();
    void        load_game();
    void        say(std::string msg, double seconds = 3.0);
    void        facing(int& x, int& y) const;

    bool         ready_ = false;
    std::string  problem_;
    tilemap::Map map_;
    Defs         defs_;
    World        world_;
    std::vector<Schedule> schedules_;
    Dialogue     anna_;
    bool         have_anna_ = false;

    tilemap::Camera2D cam_;
    DialogueRunner    talk_;
    bool              talking_ = false;
    std::size_t       choice_ = 0;
    double            typed_ = 0.0;         // characters revealed so far

    int    face_x_ = 0, face_y_ = 1;
    double step_cooldown_ = 0.0;
    Tab    tool_ = Tab::Hoe;
    int    seed_ = 0;                       // index into defs_.crops

    std::string message_;
    double      message_t_ = 0.0;
    std::optional<DayReport> last_day_;
};

} // namespace farm
