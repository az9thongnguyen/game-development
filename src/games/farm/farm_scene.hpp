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
//
//  It is also the second consumer of the BaaS, and the first one that treats the
//  backend as something a game DEPENDS on rather than demonstrates: prices arrive
//  from remote config, a live event changes them again, and the save is reconciled
//  with the cloud copy before either is trusted. Everything degrades to a playable
//  offline game — the connection is pumped from update(), never waited on, because
//  a frame that waits for a socket is the blocking loop this project does not have.
// =============================================================================
#pragma once

#include <memory>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gbaas/gbaas.h"

#include "engine/scene.hpp"
#include "games/farm/cloud.hpp"
#include "engine/tilemap/camera2d.hpp"
#include "engine/tilemap/map2.hpp"
#include "games/farm/defs.hpp"
#include "engine/tilemap/tileset.hpp"
#include "games/farm/dialogue.hpp"
#include "games/farm/controls.hpp"
#include "games/farm/theme.hpp"
#include "games/farm/world.hpp"

namespace farm {

class FarmScene : public engine::Scene {
public:
    // `map_path` and the data files are asset-relative. Everything is loaded here so
    // a failure has one place to be reported from, and the scene still runs (drawing
    // the reason) rather than launching into a black window.
    // `cfg` says where the backend is; `transport` says how to reach it, and a null
    // one means the platform's own. One constructor rather than three overloads: a
    // test drives the whole cloud path through a fake transport (the alternative is a
    // unit test that opens a socket, which is a unit test that fails on a train), and
    // the end-to-end test needs a real transport pointed at a port it chose.
    explicit FarmScene(gbaas::Config cfg = default_config(),
                       std::unique_ptr<gbaas::ITransport> transport = nullptr);

    static gbaas::Config default_config();

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

    // For tests: what loaded, and the simulation it is driving.
    [[nodiscard]] bool         ready() const { return ready_; }
    [[nodiscard]] const std::string& problem() const { return problem_; }
    [[nodiscard]] const World& world() const { return world_; }
    [[nodiscard]] const Defs&  defs()  const { return defs_; }

    // Cloud state, for tests and for the HUD (same source, so they cannot disagree).
    [[nodiscard]] const std::string& cloud_line() const { return cloud_line_; }
    // What the HUD chip actually says. render() draws THIS, so a test that reads it is
    // reading the screen rather than a parallel opinion about the screen.
    [[nodiscard]] std::string cloud_chip() const;
    [[nodiscard]] const std::string& config_problem() const { return config_problem_; }
    [[nodiscard]] bool  online()   const { return link_ == Link::Online; }
    [[nodiscard]] bool  conflict() const { return conflict_; }
    [[nodiscard]] const std::string& event_name() const { return event_name_; }
    // For tests: which way the player is facing, and where the camera put the world.
    // The camera origin is exposed so a test can convert a TILE to a screen point
    // without re-deriving the camera — the inverse arithmetic stays in the test.
    // Tiles cut from one named sheet; 0 when the theme never declared that name or
    // its image would not load. Tests use it to prove the art actually arrived.
    // Where the on-screen controls are, for the frame most recently DRAWN. Exposed so
    // a test presses the button the player would see rather than a rectangle it
    // computed for itself — a test that recomputes the layout stops testing it.
    // The conflict flag goes in because it CHANGES the layout (see controls.hpp), so a
    // test that presses "take the cloud's" has to ask for the screen the player is
    // actually looking at rather than one where that button does not exist.
    [[nodiscard]] Layout controls() const { return layout(screen_w_, screen_h_, conflict_); }

    // The dialogue panel for the frame most recently drawn, same rule and same reason:
    // a test that recomputed these rectangles would agree with itself about a screen
    // the player never saw. `talking()` goes with it — the state a test has to be able
    // to see to prove the box can still be closed.
    [[nodiscard]] bool talking() const { return talking_; }
    [[nodiscard]] Talk talk_controls() const {
        // No box when nobody is talking. `talk_layout(w, h, 0)` is a perfectly good
        // panel for a line of PROSE, so returning it here would say "there is a
        // dialogue on screen offering nothing" — which is a different thing, and the
        // one a test would then believe.
        if (!talking_) return Talk{};
        return talk_layout(screen_w_, screen_h_, static_cast<int>(talk_.choices().size()));
    }

    [[nodiscard]] std::size_t tile_count(const std::string& sheet) const {
        return sheet_of(sheet).count();
    }
    [[nodiscard]] int   seed_index() const { return seed_; }
    // Which of the four tools is held. Exposed for the same reason `seed_index` is:
    // the hotbar has answered a tap since chapter 126, and a test that could only see
    // the tool through its EFFECT would pass on a control that selected the wrong one
    // and then did the right thing by accident.
    [[nodiscard]] int   tool_index() const { return static_cast<int>(tool_); }
    [[nodiscard]] int   facing_x() const { return face_x_; }
    [[nodiscard]] int   facing_y() const { return face_y_; }
    [[nodiscard]] float camera_origin_x() const { return cam_.origin().x; }
    [[nodiscard]] float camera_origin_y() const { return cam_.origin().y; }

private:
    // What the connection is doing. Offline is the resting state, not an error: the
    // farm is a complete game with no backend at all.
    enum class Link { Offline, Connecting, Online, Failed };
    enum class Tab { Hoe = 0, Water, Seed, Harvest };

    void        load();
    void        connect();                  // guest sign-in, then everything below
    std::string device_id();                // stable per installation; created once
    void        pull_config();              // remote config + live events -> overrides
    void        sync_saves();               // download, decide, and act (or ask)
    void        push_save();                // upload the world we are holding
    void        adopt_cloud();              // accept the downloaded copy
    void        adopt_world(World w);       // install a world: NPCs, camera, message
    std::optional<World> world_from_text(const std::string& text, std::string* why) const;
    LocalSave   local_stamp() const;
    std::string bookmark_path() const;
    void        read_bookmark();
    void        write_bookmark(long long version, std::uint64_t hash);
    void        track(const char* name, const std::string& props = "{}");
    void        interact();                 // the Z/Space key
    void        sleep_now(bool collapsed);
    std::string save_path() const;
    void        save_game();
    void        load_game();
    void        say(std::string msg, double seconds = 3.0);
    void        facing(int& x, int& y) const;
    [[nodiscard]] const tilemap::Tileset& sheet_of(const std::string& name) const;
    // One moving thing, at a pixel position. false = no art, draw the circle.
    bool        draw_actor(gfx::Renderer2D& g, const std::string& name, int px, int py) const;
    bool        draw_tile(gfx::Renderer2D& g, const char* layer, std::int32_t id, int x, int y,
                          int px, int py) const;

    bool         ready_ = false;
    std::string  problem_;
    std::string  problem_art_;   // art that did not load: reported, never fatal
    tilemap::Map map_;
    // The art, and the join between the map's semantic ids and it. Both optional:
    // a missing sheet or a missing line falls back to the flat colours the game had
    // before there was any art, which is what lets a pack cover only part of a map.
    // A theme that did not parse is an EMPTY theme, not a missing one: find()
    // then answers nullptr for every id and the whole game falls back to flat
    // colour on its own. One fewer guard in the draw path, and the guard that
    // is gone is one a mutation could have deleted unnoticed (chapter 121).
    // The framebuffer size from the last render. update() runs first and does not know
    // the viewport; this is the same one-frame borrow the camera origin already makes
    // for the pointer, and for the same reason — computing it twice means keeping two
    // copies in agreement.
    int                                       screen_w_ = 0, screen_h_ = 0;
    platform::InputState                      in_{};   // the last one update() saw
    Theme                                     theme_;
    std::map<std::string, tilemap::Tileset>   tiles_;   // sheet name -> cut tiles
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

    // ---- cloud ----
    gbaas::Client client_;
    Link          link_ = Link::Offline;
    std::string   cloud_line_ = "offline";
    std::string   event_name_;
    int           overrides_applied_ = 0;
    std::string   config_problem_;          // the operator's typo, shown to whoever is playing
    // The downloaded copy is HELD, not applied, whenever the player has to choose.
    std::string   cloud_text_;
    long long     cloud_version_ = 0;
    bool          conflict_      = false;
    // True from the moment the cloud copy is asked for until the verdict lands. A save
    // made inside that window must not upload: the decision about to be taken is
    // computed from a snapshot of the cloud taken BEFORE the upload, and acting on
    // both is how a save gets sent twice — or, in the wrong order, overwritten.
    bool          syncing_       = false;
    long long     synced_version_ = 0;
    std::uint64_t synced_hash_    = 0;

    std::string message_;
    double      message_t_ = 0.0;
    std::optional<DayReport> last_day_;
};

} // namespace farm
