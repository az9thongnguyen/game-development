// =============================================================================
//  main.cpp  —  engine entry point + mode dispatch
// =============================================================================
//  Usage:
//    demo                 -> M0 engine demo (window)
//    demo --gui [hvh|hvai] [easy|medium|hard]  -> chess in the window
//    demo --tui [hvh|hvai] [easy|medium|hard]  -> chess in the terminal
//
//  main only wires things together; the GUI paths talk to the platform/engine,
//  the TUI path is pure stdin/stdout (no window), and all chess modes drive the
//  shared chess core.
// =============================================================================
#include <memory>
#include <string>

#include "engine/app.hpp"
#include "engine/assets.hpp"
#include "platform/platform.hpp"

#include "demo/demo_scene.hpp"
#include "games/chess/chess_gui.hpp"
#include "games/chess/chess_tui.hpp"

namespace {

// Parse optional "[hvh|hvai] [easy|medium|hard]" args starting at index `from`.
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

int run_window(const char* title, std::unique_ptr<engine::Scene> scene) {
    platform::Config cfg;
    cfg.title     = title;
    cfg.fb_width  = 480;
    cfg.fb_height = 270;
    cfg.scale     = 2;

    if (!platform::init(cfg)) return 1;
    platform::init_audio();
    assets::set_base_path("assets");

    engine::App app(std::move(scene));
    platform::run([&app](double dt) { app.frame(dt); });

    platform::shutdown();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = (argc > 1) ? argv[1] : "";

    if (mode == "--tui") {
        bool vs_ai = true; int depth = 4;
        parse_chess_opts(argc, argv, 2, vs_ai, depth);
        return chess::run_tui(vs_ai, depth);
    }

    if (mode == "--gui") {
        bool vs_ai = true; int depth = 4;
        parse_chess_opts(argc, argv, 2, vs_ai, depth);
        return run_window("hand-engine — chess",
                          std::make_unique<chess::ChessScene>(vs_ai, depth));
    }

    // No args: the M0 engine demo.
    return run_window("hand-engine — M0", std::make_unique<demo::DemoScene>());
}
