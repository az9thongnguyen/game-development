// =============================================================================
//  main.cpp  —  engine entry point
// =============================================================================
//  M0 final form: bring up the platform + audio seam, point the asset loader at
//  ./assets, then run the DemoScene through the engine loop. main only talks to
//  the platform + engine APIs — no SDL, no game logic here.
// =============================================================================
#include <memory>

#include "engine/app.hpp"
#include "engine/assets.hpp"
#include "platform/platform.hpp"

#include "demo/demo_scene.hpp"

int main(int /*argc*/, char** /*argv*/) {
    platform::Config cfg;
    cfg.title     = "hand-engine — M0";
    cfg.fb_width  = 480;
    cfg.fb_height = 270;
    cfg.scale     = 2;

    if (!platform::init(cfg)) {
        return 1;
    }
    platform::init_audio();           // audio seam (stub until M2)
    assets::set_base_path("assets");  // resolve asset paths under ./assets

    engine::App app(std::make_unique<demo::DemoScene>());
    platform::run([&app](double dt) { app.frame(dt); });

    platform::shutdown();
    return 0;
}
