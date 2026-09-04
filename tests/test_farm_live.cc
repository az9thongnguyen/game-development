// =============================================================================
//  tests/test_farm_live.cc  —  the farm against a real backend
// =============================================================================
//  test_farm_scene drives the game through a scripted transport: it proves the
//  game's REASONING about a backend. This proves the chain actually exists —
//  an operator changes a price through the admin API, and a running game charges
//  the new one. Between those two ends sit the seed, the HTTP routes, the SDK, the
//  override parser and the day-end sale, and any of them could be the broken link.
//
//  Same shape as test_sdk_live: Drogon runs on the main thread, a worker drives the
//  scene and quits the app when it is done. The scene is driven by calling update()
//  on a loop — which is exactly what the platform does, minus the window.
// =============================================================================
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <drogon/drogon.h>
#include <sodium.h>

#include "baas/app_config.h"
#include "baas/app_setup.h"
#include "baas/db/db.h"
#include "engine/assets.hpp"
#include "games/farm/farm_scene.hpp"
#include "tests/baas_test_util.h"

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

const platform::InputState kIdle{};

platform::InputState key(platform::Key k) {
    platform::InputState in{};
    in.key_pressed[static_cast<int>(k)] = true;
    in.key_down[static_cast<int>(k)]    = true;
    return in;
}

// Drive the scene like a frame loop until `pred` holds. Real requests are in flight,
// so this sleeps between frames rather than spinning: the point is to reach the state,
// not to measure how fast.
template <class Pred>
bool pump(farm::FarmScene& s, Pred pred) {
    for (int i = 0; i < 400 && !pred(); ++i) {
        s.update(1.0 / 60.0, kIdle);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

void clear_file(const std::string& path) { assets::write_file(path, {}); }

} // namespace

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    web::set_config(web::AppConfig{"farm-live-secret", 3600});
    assets::set_base_path(ASSET_ROOT "/assets");

    const std::string db_path = "test_farm_live.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    web::db::seed(db);

    const int         port = baastest::find_free_port();
    const std::string base = "http://127.0.0.1:" + std::to_string(port);

    drogon::app().setLogLevel(trantor::Logger::kError);
    web::register_routes();
    drogon::app().addListener("127.0.0.1", port);

    std::thread tester([&] {
        for (int i = 0; i < 200; ++i) {
            if (baastest::http("GET", base + "/healthz", {}).status == 200) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        const std::string api = "X-Api-Key: pk_demo_farm";
        const std::string sec = "X-Secret-Key: sk_demo_farm";
        const gbaas::Config cfg{base, "pk_demo_farm"};

        // ---- the operator changes a price -------------------------------------
        // Through the admin API, with the project's secret — the same call the
        // dashboard makes. The value is one line of the game's own defs format.
        CHECK(baastest::http("PUT", base + "/v1/admin/config/farm_defs", {api, sec},
                             R"({"value":"crop parsnip sell=250\n"})").status == 200);

        clear_file("saves/farm/slot1.sav");
        clear_file("saves/farm/slot1.sync");
        {
            farm::FarmScene a{cfg};
            CHECK(a.ready());
            CHECK(pump(a, [&] { return a.online() && a.defs().crop("parsnip")->sell == 250; }));
            CHECK(a.defs().crop("parsnip")->sell == 250);
            // The rest of the parsnip is exactly as the file left it. A whole-record
            // merge would have quietly reset these to the struct defaults.
            CHECK(a.defs().crop("parsnip")->days   == 4);
            CHECK(a.defs().crop("parsnip")->stages == 5);
            CHECK(a.defs().crop("parsnip")->seed   == 20);
            CHECK(a.defs().crop("turnip")->sell    == 15);
            CHECK(a.event_name().empty());   // the seeded festival is not running
        }

        // ---- the operator switches the festival on ----------------------------
        CHECK(baastest::http(
                  "POST", base + "/v1/admin/events", {api, sec},
                  R"({"key":"harvest_festival","name":"Harvest Festival",)"
                  R"("starts_at":"2000-01-01 00:00:00","ends_at":"2999-01-01 00:00:00",)"
                  R"("payload":"crop parsnip sell=900\n"})")
                  .status == 200);

        clear_file("saves/farm/slot1.sav");
        clear_file("saves/farm/slot1.sync");
        {
            farm::FarmScene b{cfg};
            CHECK(pump(b, [&] { return b.event_name() == "Harvest Festival"; }));
            // The event lands ON TOP of remote config, which landed on the file. If the
            // two were fetched together this would be 250 about half the time.
            CHECK(pump(b, [&] { return b.defs().crop("parsnip")->sell == 900; }));
            CHECK(b.defs().crop("parsnip")->sell == 900);

            // ---- a save leaves this machine and comes back on another ----------
            // Move the world somewhere unmistakable, then save. F5 writes the file and
            // uploads it in one gesture.
            for (int i = 0; i < 5; ++i) b.update(30.0, kIdle);
            const int minute = b.world().minute;
            CHECK(minute != farm::kDayStartMin);
            b.update(1.0 / 60.0, key(platform::Key::F5));
            CHECK(pump(b, [&] { return b.cloud_line().rfind("cloud v", 0) == 0; }));
            const std::string pushed = b.cloud_line();

            // A machine that has never seen this farm: no local save, no bookmark.
            clear_file("saves/farm/slot1.sav");
            clear_file("saves/farm/slot1.sync");
            farm::FarmScene c{cfg};
            CHECK(c.world().minute == farm::kDayStartMin);   // it started fresh...
            CHECK(pump(c, [&] { return c.world().minute == minute; }));
            // The copy pulled is the copy pushed. Comparing the version rather than
            // just "some save arrived" is what catches a stray write in between.
            CHECK(c.cloud_line() == pushed);
            CHECK(c.world().minute == minute);               // ...and pulled the other one
            CHECK(!c.conflict());
        }

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    clear_file("saves/farm/slot1.sav");
    clear_file("saves/farm/slot1.sync");
    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("farm_live: all tests passed\n");
    else                 std::printf("farm_live: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
