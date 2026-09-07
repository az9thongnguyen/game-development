// =============================================================================
//  tests/test_creature_pvp_live.cc  —  two players, one battle, a real socket
// =============================================================================
//  `test_netbattle` plays a thousand matches by handing two objects each other's
//  frames. This one plays ONE, over a real WebSocket, against a real Drogon server,
//  through the real SDK — and then reports the result to a ladder that computes the
//  rating itself.
//
//  What it is here to catch is everything the pure test cannot: that the server's
//  `matched` event actually carries a side and a seed, that two clients given
//  opposite sides build the SAME battle, that a frame broadcast to a room reaches
//  the peer and not the sender, and that both players reporting one match moves the
//  ratings once.
//
//  Same libcurl gate as `sdk_realtime_live`: without a WebSocket-capable curl the
//  SDK's realtime is an inert stub, and this test would drive nothing.
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
#include "engine/elo.hpp"
#include "games/creatures/defs.hpp"
#include "games/creatures/netbattle.hpp"
#include "games/creatures/pvp.hpp"
#include "games/creatures/replay.hpp"
#include "gbaas/gbaas.h"
#include "tests/baas_test_util.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

using namespace creature;

namespace {

bool read_asset(const char* path, std::string& out) {
    const auto bytes = assets::load_file(path);
    if (!bytes) return false;
    out.assign(bytes->begin(), bytes->end());
    return true;
}

}  // namespace

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    web::set_config(web::AppConfig{"integration-test-secret", 3600});
    assets::set_base_path(ASSET_ROOT "/assets");

    Dex         dex;
    std::string why;
    if (!load_dex(dex, read_asset, &why)) { std::printf("FAIL %s\n", why.c_str()); return 1; }

    const std::string db_path = "test_creature_pvp_live.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client("sqlite://" + db_path);
    web::db::set_client(db);
    web::db::run_migrations(db);
    web::db::seed(db);                       // creates the creatures demo + its ladder

    const int         port = baastest::find_free_port();
    const std::string base = "http://127.0.0.1:" + std::to_string(port);
    const std::string pk   = "pk_demo_creatures";

    drogon::app().setLogLevel(trantor::Logger::kError);
    web::register_routes();
    drogon::app().addListener("127.0.0.1", port);

    std::thread tester([&] {
        for (int i = 0; i < 200; ++i) {
            if (baastest::http("GET", base + "/healthz", {}).status == 200) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        // The SAME PvpClient `--pvp` runs. If this file glued the protocol to the
        // socket itself, the thing under test would not be the thing that ships.
        PvpClient a(dex, {base, pk}), b(dex, {base, pk});
        a.start(make_party(dex, {{1, 20}, {5, 18}, {9, 22}}));
        b.start(make_party(dex, {{4, 21}, {8, 19}, {12, 20}}));

        auto tick = [&](int n) {
            for (int i = 0; i < n; ++i) {
                a.update();
                b.update();
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
            }
        };

        // Let A reach the queue first, so the FIFO pairs these two and the sides are
        // predictable enough to assert on.
        for (int i = 0; i < 300 && a.state() != PvpClient::State::Queued; ++i) { a.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(8)); }
        CHECK(a.state() == PvpClient::State::Queued);
        tick(400);   // far more than a battle needs; the pure test's longest was 23 turns

        // ---- the server decided who was who --------------------------------
        CHECK(a.user_id() != 0 && b.user_id() != 0 && a.user_id() != b.user_id());
        CHECK(!a.room().empty() && a.room() == b.room());
        CHECK(a.net().side() != b.net().side());
        CHECK(a.opponent_id() == b.user_id());
        CHECK(b.opponent_id() == a.user_id());

        // ---- ...and the two machines agree about the battle -----------------
        if (a.state() != PvpClient::State::Done || b.state() != PvpClient::State::Done)
            std::printf("      a=%d (%s)  b=%d (%s)\n", static_cast<int>(a.state()),
                        a.problem().c_str(), static_cast<int>(b.state()),
                        b.problem().c_str());
        CHECK(a.state() == PvpClient::State::Done);
        CHECK(b.state() == PvpClient::State::Done);
        CHECK(a.net().phase() == NetPhase::Over);
        CHECK(b.net().phase() == NetPhase::Over);
        CHECK(hash(a.net().battle()) == hash(b.net().battle()));
        CHECK(a.net().winner() >= 0);
        CHECK(a.net().winner() == b.net().winner());
        CHECK(a.net().won() != b.net().won());
        CHECK(a.net().tape().turns.size() == b.net().tape().turns.size());
        CHECK(a.net().tape().turns.size() >= 2);
        std::printf("  a real match: %zu turns over a socket, side %d won\n",
                    a.net().tape().turns.size(), a.net().winner());

        // The tape a LIVE match produced is the same artefact the offline verifier
        // reads. Chapter 138's file format and this chapter's protocol are the same
        // thing seen from two sides, and this is where that stops being a claim.
        std::string wa;
        const std::string tape = a.tape_text();
        CHECK(!tape.empty());
        CHECK(tape == b.tape_text());
        Replay parsed;
        CHECK(read_replay(dex, tape, parsed, &wa));
        CHECK(verify(dex, parsed).ok);

        // ...and the client STORED it, in the BaaS replay store that has existed
        // since chapter 100 and until now had exactly one consumer. Fetching it back
        // is what makes "it was uploaded" more than a callback that fired.
        CHECK(a.replay_id() != 0);
        {
            std::string fetched;
            bool done = false;
            a.client().replays().get(a.replay_id(), [&](gbaas::Result<gbaas::Replay> r) {
                if (r) fetched = r->data;
                done = true;
            });
            for (int i = 0; i < 300 && !done; ++i) { a.client().update();
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            CHECK(fetched == tape);
            Replay back;
            CHECK(read_replay(dex, fetched, back, &wa));
            CHECK(verify(dex, back).ok);
        }

        // ---- the ladder ----------------------------------------------------
        // BOTH clients reported, because both played. Exactly one report applied.
        CHECK(a.rating_applied() != b.rating_applied());
        // The one that applied carries a delta; the one that did not carries none,
        // because the stored delta belongs to whoever got there first.
        CHECK((a.rating_applied() ? a.rating_delta() : b.rating_delta()) != 0);
        CHECK((a.rating_applied() ? b.rating_delta() : a.rating_delta()) == 0);
        const int winner_rating = a.net().won() ? a.rating() : b.rating();
        const int loser_rating  = a.net().won() ? b.rating() : a.rating();
        CHECK(winner_rating + loser_rating == 2 * engine::kEloStart);
        CHECK(winner_rating == engine::kEloStart + engine::kEloK / 2);
        CHECK(loser_rating  == engine::kEloStart - engine::kEloK / 2);
        std::printf("  the ladder: %d vs %d (both reported, applied once)\n",
                    winner_rating, loser_rating);

        // ...and the board agrees, ordered.
        {
            gbaas::Board board;
            bool done = false;
            a.client().leaderboard("creature_elo").top(10, [&](gbaas::Result<gbaas::Board> r) {
                if (r) board = *r;
                done = true;
            });
            for (int i = 0; i < 300 && !done; ++i) { a.client().update();
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            CHECK(board.entries.size() == 2);
            if (board.entries.size() == 2) {
                CHECK(board.entries[0].value == winner_rating);
                CHECK(board.entries[1].value == loser_rating);
            }
        }

        a.client().realtime().disconnect();
        b.client().realtime().disconnect();
        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    if (g_failures == 0) std::printf("creature_pvp_live: all tests passed\n");
    else                 std::printf("creature_pvp_live: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
