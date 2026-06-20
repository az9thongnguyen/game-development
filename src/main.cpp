// =============================================================================
//  main.cpp  —  engine entry point + mode dispatch
// =============================================================================
//  Usage:
//    demo                 -> M0 engine demo (window)
//    demo --gui           -> chess in the window      (added at M1 Step 6)
//    demo --tui [hvh|hvai] [easy|medium|hard]  -> chess in the terminal
//
//  main only wires things together; the GUI path talks to the platform/engine,
//  the TUI path is pure stdin/stdout (no window), and both drive the shared
//  chess core.
// =============================================================================
#include <memory>
#include <string>

#include "engine/app.hpp"
#include "engine/assets.hpp"
#include "platform/platform.hpp"

#include "demo/demo_scene.hpp"
#include "games/chess/chess_tui.hpp"

namespace {

int run_engine_demo() {
    platform::Config cfg;
    cfg.title     = "hand-engine — M0";
    cfg.fb_width  = 480;
    cfg.fb_height = 270;
    cfg.scale     = 2;

    if (!platform::init(cfg)) return 1;
    platform::init_audio();
    assets::set_base_path("assets");

    engine::App app(std::make_unique<demo::DemoScene>());
    platform::run([&app](double dt) { app.frame(dt); });

    platform::shutdown();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const std::string mode = (argc > 1) ? argv[1] : "";

    if (mode == "--tui") {
        bool vs_ai = true;
        int  depth = 4;  // Medium
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            if      (a == "hvh")    vs_ai = false;
            else if (a == "hvai")   vs_ai = true;
            else if (a == "easy")   depth = 2;
            else if (a == "medium") depth = 4;
            else if (a == "hard")   depth = 6;
        }
        return chess::run_tui(vs_ai, depth);
    }

    // mode == "--gui" will launch the chess window at M1 Step 6; until then the
    // default and --gui both run the M0 engine demo.
    return run_engine_demo();
}
