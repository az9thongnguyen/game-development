// =============================================================================
//  main.cpp  —  engine entry point + mode dispatch
// =============================================================================
//  Usage:
//    demo                 -> M0 engine demo (retro 480x270 window)
//    demo --gui [hvh|hvai] [easy|medium|hard]  -> chess (large, crisp window)
//    demo --tui [hvh|hvai] [easy|medium|hard]  -> chess in the terminal
// =============================================================================
#include <memory>
#include <string>
#include <vector>

#include "engine/app.hpp"
#include "engine/assets.hpp"
#include "engine/project/project.hpp"
#include "platform/platform.hpp"

#include "demo/demo_scene.hpp"
#include "games/chess/chess_gui.hpp"
#include "games/chess/chess_tui.hpp"
#include "games/fps/raycast_scene.hpp"
#include "games/viz3d/scene3d.hpp"
#include "games/viz3d/editor_scene.hpp"
#include "games/iso/iso_scene.hpp"
#include "games/editor/editor_scene.hpp"
#include "games/colony/colony_scene.hpp"
#include "games/studio/studio_scene.hpp"
#include "games/sandbox/sandbox_scene.hpp"
#include "games/maplab/maplab_scene.hpp"
#include "games/anim/anim_scene.hpp"
#include "games/audio/audio_scene.hpp"
#include "games/fx/fx_scene.hpp"
#include "games/light/light_scene.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#include "gbaas/client.h"
#include "games/runner/worker.hpp"

namespace {

void parse_chess_opts(int argc, char** argv, int from, bool& vs_ai, int& depth) {
    for (int i = from; i < argc; ++i) {
        const std::string a = argv[i];
        if      (a == "hvh")    vs_ai = false;
        else if (a == "hvai")   vs_ai = true;
        else if (a == "easy")   depth = 2;
        else if (a == "medium") depth = 4;
        else if (a == "hard")   depth = 6;
    }
}

// SSAA factor for 2D scenes. Native gets full 2× supersampling; the web build
// stays at 1× (per-primitive AA + AA fonts still apply) because 4× software fill
// in WASM is too costly for a smooth frame rate.
#ifdef __EMSCRIPTEN__
constexpr int kAA = 1;
#else
constexpr int kAA = 2;
#endif

int run_window(const platform::Config& cfg, std::unique_ptr<engine::Scene> scene) {
    if (!platform::init(cfg)) return 1;
    platform::init_audio();

    engine::App app(std::move(scene));
    platform::run([&app](double dt) { app.frame(dt); });

    platform::shutdown();
    return 0;
}

// Launch a reference-game entry scene by its manifest id. Single source of truth
// shared by the --fps flag and the --project path, so a project manifest selects a
// game without editing this dispatch. Extend the table (and kKnownEntries) when a
// new entry scene is added. ponytail: starts with the one Horizon 0 reference game.
int launch_entry(const std::string& entry) {
    if (entry == "fps") {
        platform::Config cfg;
        cfg.title     = "hand-engine — fps";
        cfg.fb_width  = 640;
        cfg.fb_height = 400;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        return run_window(cfg, std::make_unique<fps::RaycastScene>());
    }
    std::fprintf(stderr, "unknown entry scene: %s\n", entry.c_str());
    return 1;
}

// The entry ids a project manifest may name — kept in sync with launch_entry.
const std::vector<std::string> kKnownEntries = {"fps"};

// Load a game.project manifest through the assets:: seam, validate it, and either
// print an inspection report (headless, no window) or launch its entry scene.
int launch_project(const std::string& path, bool inspect_only) {
    auto bytes = assets::load_file(path);
    if (!bytes) {
        std::fprintf(stderr, "project: cannot read '%s'\n", path.c_str());
        return 1;
    }
    auto proj = engine::parse_project(std::string(bytes->begin(), bytes->end()));
    if (!proj) {
        std::fprintf(stderr, "project: '%s' is not a valid gameproject1 manifest\n",
                     path.c_str());
        return 1;
    }
    const auto errs = engine::validate(*proj, kKnownEntries);

    if (inspect_only) {
        std::printf("project: %s\n  name   %s\n  schema %d\n  entry  %s\n",
                    path.c_str(), proj->name.c_str(), proj->schema, proj->entry.c_str());
        if (errs.empty()) { std::printf("  status OK\n"); return 0; }
        std::printf("  status %zu problem(s):\n", errs.size());
        for (const auto& e : errs) std::printf("    - %s\n", e.c_str());
        return 1;
    }

    if (!errs.empty()) {
        std::fprintf(stderr, "project: '%s' failed validation:\n", path.c_str());
        for (const auto& e : errs) std::fprintf(stderr, "  - %s\n", e.c_str());
        return 1;
    }
    return launch_entry(proj->entry);
}

} // namespace

int main(int argc, char** argv) {
    // Resolve asset paths under ./assets BEFORE any scene is constructed (scenes
    // load images/files in their constructors).
    assets::set_base_path("assets");

    const std::string mode = (argc > 1) ? argv[1] : "";

    if (mode == "--tui") {
        bool vs_ai = true; int depth = 4;
        parse_chess_opts(argc, argv, 2, vs_ai, depth);
        return chess::run_tui(vs_ai, depth);
    }

    if (mode == "--gui") {
        bool vs_ai = true; int depth = 4;
        parse_chess_opts(argc, argv, 2, vs_ai, depth);
        platform::Config cfg;
        cfg.title     = "hand-engine — chess";
        cfg.fb_width  = 980;   // large + crisp (1:1, smooth scaling, HiDPI)
        cfg.fb_height = 720;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<chess::ChessScene>(vs_ai, depth));
    }

    if (mode == "--fps") {
        return launch_entry("fps");
    }

    // Launch a game from a versioned game.project manifest (asset-relative path),
    // instead of a hard-coded scene flag: this is the Horizon 0 golden path.
    if (mode == "--project") {
        if (argc < 3) { std::fprintf(stderr, "usage: demo --project <path>\n"); return 1; }
        return launch_project(argv[2], /*inspect_only=*/false);
    }

    // Headless: validate a manifest and print an inspection report, no window.
    if (mode == "--project-inspect") {
        if (argc < 3) { std::fprintf(stderr, "usage: demo --project-inspect <path>\n"); return 1; }
        return launch_project(argv[2], /*inspect_only=*/true);
    }

    if (mode == "--3d") {
        platform::Config cfg;
        cfg.title     = "hand-engine — 3D core";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<viz3d::Scene3D>());
    }

    if (mode == "--viz3d") {
        platform::Config cfg;
        cfg.title     = "hand-engine — viz3d sandbox";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<viz3d::EditorScene>());
    }

    if (mode == "--iso") {
        platform::Config cfg;
        cfg.title     = "hand-engine — iso farm sim";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<iso::IsoScene>());
    }

    if (mode == "--editor") {
        platform::Config cfg;
        cfg.title     = "hand-engine — editor (UI + physics)";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<editor::EditorScene>());
    }

    if (mode == "--colony") {
        platform::Config cfg;
        cfg.title     = "hand-engine — colony (ECS + jobs + UI)";
        cfg.fb_width  = 1000;
        cfg.fb_height = 760;   // room for the taller design-system panel
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<colony::ColonyScene>());
    }

    if (mode == "--studio") {
        platform::Config cfg;
        cfg.title     = "hand-engine — texture lab";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<studio::StudioScene>());
    }

    if (mode == "--sandbox") {
        platform::Config cfg;
        cfg.title     = "hand-engine — sandbox";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<sandbox::SandboxScene>());
    }

    if (mode == "--maplab") {
        platform::Config cfg;
        cfg.title     = "hand-engine — map lab";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<maplab::MaplabScene>());
    }

    if (mode == "--fx") {
        platform::Config cfg;
        cfg.title     = "hand-engine — particle fx";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<fx::FxScene>());
    }

    if (mode == "--light") {
        platform::Config cfg;
        cfg.title     = "hand-engine — 2D lighting";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = 1;   // lights are soft; skip SSAA to keep the per-pixel add cheap
        return run_window(cfg, std::make_unique<lightdemo::LightScene>());
    }

    if (mode == "--audio") {
        platform::Config cfg;
        cfg.title     = "hand-engine — audio mixer";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<audiodemo::AudioScene>());
    }

    if (mode == "--anim") {
        platform::Config cfg;
        cfg.title     = "hand-engine — sprite animation";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<animdemo::AnimScene>());
    }

    // Headless test-run worker: polls a BaaS coordinator, runs claimed sandbox
    // scenarios, and posts results. Links the engine + SDK (the BaaS may not) — a
    // plain poll loop, not a windowed scene, so it bypasses platform::init.
    if (mode == "--runner") {
        if (argc < 4) {
            std::fprintf(stderr, "usage: demo --runner <base_url> <api_key>\n");
            return 1;
        }
        gbaas::Client c({argv[2], argv[3]});
        std::fprintf(stderr, "runner: polling %s for test runs (Ctrl-C to stop)\n", argv[2]);
        for (;;) {
            if (!runner::process_one(c))
                std::this_thread::sleep_for(std::chrono::seconds(1));  // idle: back off
        }
    }

    // No args: the M0 engine demo (retro 480x270, nearest scaling).
    platform::Config cfg;
    cfg.title     = "hand-engine — M0";
    cfg.fb_width  = 480;
    cfg.fb_height = 270;
    cfg.scale     = 2;   // retro: nearest scaling, no SSAA (keep the chunky M0 look)
    return run_window(cfg, std::make_unique<demo::DemoScene>());
}
