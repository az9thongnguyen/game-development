// =============================================================================
//  tests/test_sdk_live.cc  —  end-to-end: the real SDK against a real server
// =============================================================================
//  The unit test (test_sdk_client) uses a fake transport; this exercises the
//  actual libcurl transport over the wire against a live Drogon app. Boots the
//  server on the main thread; a worker drives a gbaas::Client, pumping update()
//  until each async call completes, then quits the app.
// =============================================================================
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include <curl/curl.h>
#include <drogon/drogon.h>
#include <sodium.h>

#include "baas/app_config.h"
#include "baas/app_setup.h"
#include "baas/db/db.h"
#include "gbaas/gbaas.h"
#include "games/runner/worker.hpp"
#include "tests/baas_test_util.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

// Pump the client until `done` flips or we time out.
static bool pump(gbaas::Client& c, const bool& done) {
    for (int i = 0; i < 400 && !done; ++i) {
        c.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done;
}

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    web::set_config(web::AppConfig{"live-test-secret", 3600});

    const std::string db_path = "test_sdk_live.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    const std::string pk = web::db::seed(db);

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

        gbaas::Client c({base, pk});

        // guest login
        gbaas::Result<gbaas::Session> ses;
        bool                          done = false;
        c.auth().guest([&](gbaas::Result<gbaas::Session> r) { ses = r; done = true; });
        CHECK(pump(c, done));
        CHECK(ses && ses->is_guest);
        CHECK(!c.token().empty());

        // submit a score
        gbaas::Result<gbaas::Rank> sub;
        done = false;
        c.leaderboard("colony_high").submit(4200, [&](gbaas::Result<gbaas::Rank> r) { sub = r; done = true; });
        CHECK(pump(c, done));
        CHECK(sub && sub->value == 4200 && sub->rank == 1 && sub->updated);

        // read the board
        gbaas::Result<gbaas::Board> bd;
        done = false;
        c.leaderboard("colony_high").top(10, [&](gbaas::Result<gbaas::Board> r) { bd = r; done = true; });
        CHECK(pump(c, done));
        CHECK(bd && bd->entries.size() == 1 && bd->entries[0].value == 4200);

        // my rank
        gbaas::Result<gbaas::Rank> me;
        done = false;
        c.leaderboard("colony_high").me([&](gbaas::Result<gbaas::Rank> r) { me = r; done = true; });
        CHECK(pump(c, done));
        CHECK(me && me->value == 4200 && me->rank == 1);

        // cloud save round-trip (real transport)
        gbaas::Result<gbaas::SaveMeta> sm;
        done = false;
        c.saves().put("colony", R"({"hp":7})", [&](gbaas::Result<gbaas::SaveMeta> r) { sm = r; done = true; });
        CHECK(pump(c, done));
        CHECK(sm && sm->version == 1);
        gbaas::Result<gbaas::Save> sv;
        done = false;
        c.saves().get("colony", [&](gbaas::Result<gbaas::Save> r) { sv = r; done = true; });
        CHECK(pump(c, done));
        CHECK(sv && sv->data == R"({"hp":7})");

        // inventory grant/consume (real transport)
        gbaas::Result<gbaas::Item> ig;
        done = false;
        c.inventory().grant("wood", 10, [&](gbaas::Result<gbaas::Item> r) { ig = r; done = true; });
        CHECK(pump(c, done));
        CHECK(ig && ig->qty == 10);
        gbaas::Result<gbaas::Item> ic;
        done = false;
        c.inventory().consume("wood", 4, [&](gbaas::Result<gbaas::Item> r) { ic = r; done = true; });
        CHECK(pump(c, done));
        CHECK(ic && ic->qty == 6);

        // remote config / live events / analytics (real transport)
        gbaas::Result<std::vector<gbaas::ConfigEntry>> cf;
        done = false;
        c.config().all([&](gbaas::Result<std::vector<gbaas::ConfigEntry>> r) { cf = r; done = true; });
        CHECK(pump(c, done));
        CHECK(cf && !cf->empty());
        gbaas::Result<std::vector<gbaas::LiveEvent>> le;
        done = false;
        c.events().active([&](gbaas::Result<std::vector<gbaas::LiveEvent>> r) { le = r; done = true; });
        CHECK(pump(c, done));
        CHECK(le && le->size() >= 1);
        gbaas::Result<bool> tk;
        done = false;
        c.analytics().track("test.event", "{}", [&](gbaas::Result<bool> r) { tk = r; done = true; });
        CHECK(pump(c, done));
        CHECK(tk && *tk);

        // asset registry round-trip (project-scoped; api key only)
        gbaas::Result<gbaas::AssetMeta> am;
        done = false;
        c.assets().put("level_00.map", "level", "fpsmap1\nsize 2 1\nrow 1 1\n",
                       [&](gbaas::Result<gbaas::AssetMeta> r) { am = r; done = true; });
        CHECK(pump(c, done));
        CHECK(am && am->version == 1 && am->kind == "level");
        gbaas::Result<gbaas::Asset> ag;
        done = false;
        c.assets().get("level_00.map", [&](gbaas::Result<gbaas::Asset> r) { ag = r; done = true; });
        CHECK(pump(c, done));
        CHECK(ag && ag->data == "fpsmap1\nsize 2 1\nrow 1 1\n");
        gbaas::Result<std::vector<gbaas::AssetMeta>> al;
        done = false;
        c.assets().list([&](gbaas::Result<std::vector<gbaas::AssetMeta>> r) { al = r; done = true; });
        CHECK(pump(c, done));
        CHECK(al && al->size() == 1 && (*al)[0].name == "level_00.map");
        gbaas::Result<bool> ard;
        done = false;
        c.assets().remove("level_00.map", [&](gbaas::Result<bool> r) { ard = r; done = true; });
        CHECK(pump(c, done));
        CHECK(ard && *ard);

        // full test-runner loop: submit a scenario -> the worker claims + runs it
        // headlessly -> poll the result (the whole Track-C sub-project end to end)
        gbaas::Result<gbaas::TestRun> tr;
        done = false;
        c.testruns().submit(
            "sandbox1\nbounds 936 560\ne x=100 y=100 rot=0 color=dcc878 w=24 h=24\n",
            "steps=10;expect_alive=1",
            [&](gbaas::Result<gbaas::TestRun> r) { tr = r; done = true; });
        CHECK(pump(c, done));
        CHECK(tr && tr->status == "pending");
        const long long run_id = tr->id;
        CHECK(runner::process_one(c));                 // worker: claim -> run_scenario -> complete
        gbaas::Result<gbaas::TestRun> fin;
        done = false;
        c.testruns().get(run_id, [&](gbaas::Result<gbaas::TestRun> r) { fin = r; done = true; });
        CHECK(pump(c, done));
        CHECK(fin && fin->status == "passed");
        CHECK(runner::process_one(c) == false);        // nothing left pending

        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    baastest::cleanup_db(db_path);
    curl_global_cleanup();
    if (g_failures == 0) std::printf("sdk_live: all tests passed\n");
    else                 std::printf("sdk_live: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
