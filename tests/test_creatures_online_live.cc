// =============================================================================
//  tests/test_creatures_online_live.cc  —  a rated match played by a HAND
// =============================================================================
//  `test_creature_pvp_live` proves the protocol: two headless clients, one real
//  socket, one real server, and both machines agreeing about the battle. What it
//  cannot prove is the thing a player cares about — that any of it is reachable.
//  Both of its clients pick their actions with `choose`, which is an AI, and an AI
//  never presses a button.
//
//  So this file replaces one of them with the actual game screen. `CreaturesScene` is
//  driven the only way it should be: a framebuffer, a pointer, and taps at the
//  rectangles the renderer reported. It never calls `PvpClient::act` — it taps Fight
//  and then a move, and the action reaches the wire because the screen is wired to it.
//
//  The opponent stays the headless client, deliberately: the other side of this match
//  is the same code `--pvp` runs, so what is under test is the SCREEN and not a second
//  copy of the protocol.
//
//  Same gate as its sibling — without a WebSocket-capable libcurl there is no
//  transport for a match to happen over.
// =============================================================================
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <drogon/drogon.h>
#include <sodium.h>

#include "baas/app_config.h"
#include "baas/app_setup.h"
#include "baas/db/db.h"
#include "engine/assets.hpp"
#include "engine/renderer2d.hpp"
#include "engine/text/font.hpp"
#include "games/creatures/creatures_scene.hpp"
#include "games/creatures/pvp.hpp"
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

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    if (sodium_init() < 0) { std::printf("FAIL: libsodium init\n"); return 1; }
    web::set_config(web::AppConfig{"integration-test-secret", 3600});
    assets::set_base_path(ASSET_ROOT "/assets");

    const std::string db_path = "test_creatures_online_live.db";
    baastest::cleanup_db(db_path);
    auto db = web::db::make_db_client(baastest::db_url(db_path));
    web::db::set_client(db);
    web::db::run_migrations(db);
    web::db::seed(db);

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

        constexpr int PW = 640, PH = 360;
        std::vector<std::uint32_t> buf(static_cast<std::size_t>(PW) * PH, 0);
        platform::Framebuffer fb{buf.data(), PW, PH, PW};

        CreaturesScene scene;
        CHECK(scene.ready());
        if (!scene.ready()) { drogon::app().quit(); return; }

        // No font: this test is about where a tap lands, and `Renderer2D` falls back to
        // the embedded 8x8 face. A missing Inter.ttf must not be what fails here.
        const auto frame = [&](const platform::InputState& in) {
            scene.update(1.0 / 60.0, in);
            for (auto& p : buf) p = 0;
            gfx::Renderer2D r(fb, 1);
            const engine::Context ctx{r, in, 1.0 / 60.0, 0.0, 0.0, nullptr};
            scene.render(ctx);
        };
        const auto idle = [&] { frame(platform::InputState{}); };
        // A press is TWO frames: `pressed` is an edge, and a helper that only sent the
        // edge would leave `down` false, which is what a held control reads.
        const auto tap = [&](Box b) {
            if (b.empty()) return false;
            for (int i = 0; i < 2; ++i) {
                platform::InputState in{};
                in.mouse_x = b.x + b.w / 2;
                in.mouse_y = b.y + b.h / 2;
                in.mouse_down[static_cast<int>(platform::MouseButton::Left)]    = true;
                in.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = (i == 0);
                frame(in);
            }
            idle();
            return true;
        };

        idle();   // one render, so the layout the taps use is the one that was drawn

        // ---- the player changes their mind first ------------------------------
        // Queue, then Cancel. What this really asks is whether Cancel reached the
        // SERVER: a client that merely stops updating leaves a ghost in the queue, and
        // the next player to look for a match is paired with somebody who is not there.
        // The match below is what proves it — with a ghost still queued, the real
        // opponent would be matched with it and this test would time out, not play.
        const Layout over = scene.controls();
        CHECK(!over.online.empty());
        // Started from the SCREEN, with the test's server as the config. The button on
        // a shipped build uses `default_online_config()`; what is under test here is
        // everything after that, so the URL is the one part supplied from outside.
        CHECK(scene.start_online({base, pk}));
        for (int i = 0; i < 400; ++i) {
            idle();
            if (scene.online() && scene.online()->state() == PvpClient::State::Queued) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        CHECK(scene.online() != nullptr);
        CHECK(scene.online()->state() == PvpClient::State::Queued);
        CHECK(tap(scene.controls().back));            // Cancel, by tapping it
        CHECK(scene.online() == nullptr);
        CHECK(scene.mode() == Mode::Overworld);

        const Dex& dex = scene.dex();

        // ---- a client that cancelled must be OUT of the server's queue --------
        // Kept alive and connected on purpose. If Cancel did not reach the server, this
        // one is still in the queue and the two clients below would be matched with it
        // instead of with each other — which is precisely the failure a player sees as
        // "my opponent never moved". Dropping the socket would clear the queue too, and
        // that is why `cancel()` no longer does: a guard another line already covers is
        // a guard no test can tell from its own absence.
        PvpClient ghost(dex, {base, pk});
        ghost.start(make_party(dex, {{2, 5}, {6, 5}}));
        for (int i = 0; i < 400 && ghost.state() != PvpClient::State::Queued; ++i) {
            ghost.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        CHECK(ghost.state() == PvpClient::State::Queued);
        ghost.cancel();
        CHECK(ghost.state() == PvpClient::State::Idle);
        CHECK(ghost.client().realtime().connected());   // still there, just not looking

        // ---- the opponent is the headless client `--pvp` runs ----------------
        PvpClient ai(dex, {base, pk});
        // A team the player's fresh party can trade blows with. The first version of
        // this brought levels 19-21 against a level-5 starter and the match was over in
        // ONE exchange — which passed every assertion about the protocol and proved
        // nothing about a player taking turns.
        ai.start(make_party(dex, {{4, 5}, {8, 5}, {12, 5}}));

        // The OPPONENT queues first, so the server gives the PLAYER side 1. Deliberate:
        // with the player on side 0, every assertion about `my_side()` passes against a
        // screen hard-coded to 0 — which is what the screen was before this chapter, and
        // a mutation that hard-codes it again survived until this order changed.
        for (int i = 0; i < 400 && ai.state() != PvpClient::State::Queued; ++i) {
            ai.update();
            idle();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }
        CHECK(ai.state() == PvpClient::State::Queued);

        // An auto-playing client is NEVER waiting for a screen, and refuses an action
        // pushed into it. These are the two halves of `waiting_for_action`, asked of the
        // client with the other setting — without them, `--pvp` and the screen share a
        // function whose flag only one of them ever exercises.
        CHECK(ai.auto_play());
        CHECK(!ai.waiting_for_action());
        CHECK(!ai.act(Action{Action::Kind::Move, 0}));

        // ...and now the peer is driven BY HAND too. Not to test the AI — to make the
        // "I have acted and the opponent has not" state exist at all. With auto-play on,
        // a client acts inside the very `update()` that builds the battle, so the peer
        // is always ahead and that state lasts less than one tap. It is also a second
        // caller of `act()`, from a client that is not a screen.
        ai.set_auto_play(false);

        CHECK(scene.start_online({base, pk}));
        CHECK(scene.mode() == Mode::Online);

        // ---- play it, by tapping ---------------------------------------------
        int taps = 0, turns = 0, refused = 0;
        bool saw_menu = false, saw_moves = false, saw_between = false;
        bool peer_owes = false;   // the peer has a turn to answer and has not answered it
        for (int i = 0; i < 1200 && scene.mode() != Mode::Ack; ++i) {
            ai.update();
            ghost.update();          // kept alive: a ghost that stopped polling proves nothing
            const PvpClient* me = scene.online();
            CHECK(ghost.state() == PvpClient::State::Idle);   // never matched
            // `waiting_for_action` is exactly "the protocol owes an action and I am not
            // going to invent one" — not "a match is running". Asked of both clients.
            if (me && me->state() == PvpClient::State::Playing)
                CHECK(me->waiting_for_action() == (me->net().phase() == NetPhase::MyTurn));
            CHECK(ai.waiting_for_action() ==
                  (ai.state() == PvpClient::State::Playing &&
                   ai.net().phase() == NetPhase::MyTurn));

            if (me && me->waiting_for_action()) {
                const Layout l = scene.controls();
                if (scene.mode() == Mode::Menu) {
                    saw_menu = true;
                    CHECK(tap(l.cell[0]));            // Fight
                    ++taps;
                } else if (scene.mode() == Mode::Moves) {
                    saw_moves = true;
                    CHECK(tap(l.cell[0]));            // the first move
                    ++taps;
                    ++turns;
                    peer_owes = true;   // the player has acted; the peer has not
                } else {
                    idle();
                }
            } else if (peer_owes && !saw_between && me &&
                       me->state() == PvpClient::State::Playing &&
                       scene.mode() == Mode::Menu) {
                // A tap BETWEEN turns must do nothing but SAY so. Deterministic because
                // the peer is held: nothing can arrive to resolve the turn under it.
                saw_between = true;
                CHECK(!me->waiting_for_action());
                CHECK(tap(scene.controls().cell[0]));
                ++taps;
                ++refused;
                CHECK(scene.message().find("Waiting") != std::string::npos);
                CHECK(scene.mode() == Mode::Menu);      // the move list did NOT open
                CHECK(me->net().phase() == NetPhase::Waiting);
            } else {
                idle();
            }

            // ...and now the peer answers, by the same door the screen uses.
            if (peer_owes && ai.waiting_for_action() && (saw_between || turns > 1)) {
                // Asked BOTH ways at the one moment it can be: same client, same phase,
                // no update in between, so the only thing that changed is the flag.
                // Without this the flag is never exercised on a client that is Playing
                // and owes an action, which is the only state it means anything in.
                ai.set_auto_play(true);
                CHECK(!ai.waiting_for_action());
                CHECK(!ai.act(Action{Action::Kind::Move, 0}));
                ai.set_auto_play(false);
                CHECK(ai.waiting_for_action());
                CHECK(ai.act(choose(dex, ai.net().battle(), ai.net().side())));
                peer_owes = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(6));
        }

        // The opponent needs a few more ticks to store its tape and report: the loop
        // above stops when the PLAYER's screen is done, and the two finish separately.
        for (int i = 0; i < 400 && ai.state() != PvpClient::State::Done; ++i) {
            ai.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        }

        // ---- what a hand actually did -----------------------------------------
        CHECK(saw_menu);
        CHECK(saw_moves);
        CHECK(saw_between);         // ...and the refusal above was actually exercised
        CHECK(refused == 1);
        CHECK(turns >= 2);          // a match, not a single exchange
        CHECK(taps >= turns * 2);   // at least a Fight and a move for every turn
        std::printf("  the player took %d turns with %d taps\n", turns, taps);

        const PvpClient* me = scene.online();
        CHECK(me != nullptr);
        if (!me) { drogon::app().quit(); return; }
        CHECK(scene.mode() == Mode::Ack);
        CHECK(me->state() == PvpClient::State::Done);
        CHECK(me->problem().empty());
        CHECK(ai.state() == PvpClient::State::Done);

        // Both machines agree, which is the protocol's claim — asserted here too
        // because a screen that quietly played a DIFFERENT battle would still tap.
        CHECK(me->net().phase() == NetPhase::Over);
        CHECK(hash(me->net().battle()) == hash(ai.net().battle()));
        CHECK(me->net().winner() == ai.net().winner());
        CHECK(me->net().won() != ai.net().won());
        CHECK(me->room() == ai.room());
        CHECK(me->net().side() != ai.net().side());

        // The screen showed the player THEIR side of it. Every earlier check would pass
        // just as happily against a screen hard-coded to side 0, which is exactly what
        // it was before this chapter — and the server hands out sides, not the client.
        CHECK(scene.my_side() == me->net().side());
        CHECK(scene.my_side() == 1);   // and it is NOT 0, which is the whole point

        // Every turn in the RECORDING was one the player tapped a move for. This is the
        // claim `taps == turns * 2` was reaching for and could not make race-free: the
        // tape is what the protocol resolved, and it has exactly as many turns as the
        // screen sent actions for.
        CHECK(static_cast<int>(me->net().tape().turns.size()) == turns);
        CHECK(&scene.shown_battle() == &me->net().battle());
        {
            const Creature& shown = scene.shown_battle().side[scene.my_side()].now();
            bool mine = false;
            for (int i = 0; i < kPartySize; ++i)
                if (scene.world().party.member[i].species == shown.species) mine = true;
            CHECK(mine);   // the creature on the player's side came from the player's party
        }

        // Cancelling a match already in progress is a no-op: `cancel` is for the QUEUE,
        // and a client that disconnected itself mid-battle would desync its peer.
        {
            const PvpClient::State was = ai.state();
            ai.cancel();
            CHECK(ai.state() == was);
        }

        // The rating is on the screen that ends the match, which is the whole point of
        // it being RATED. Both clients report; exactly one report moves the ladder.
        CHECK(me->rating() != 0);
        CHECK(me->rating_applied() != ai.rating_applied());
        std::printf("  rating %d (%+d, %s)\n", me->rating(), me->rating_delta(),
                    me->rating_applied() ? "applied" : "already counted");

        // ...and Continue, tapped, puts the player back on the route with the session
        // gone and the party untouched — a rated match is not a wild one, and ending it
        // must not heal anybody.
        const int hp = scene.world().party.member[0].hp;
        CHECK(tap(scene.controls().ack));
        CHECK(scene.online() == nullptr);
        CHECK(scene.mode() == Mode::Overworld);
        CHECK(scene.world().party.member[0].hp == hp);

        ai.client().realtime().disconnect();
        drogon::app().quit();
    });

    drogon::app().run();
    tester.join();

    if (g_failures == 0) std::printf("creatures_online_live: all tests passed\n");
    else                 std::printf("creatures_online_live: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
