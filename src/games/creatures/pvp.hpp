// =============================================================================
//  games/creatures/pvp.hpp  —  a rated match, played by a headless client
// =============================================================================
//  `netbattle.hpp` is the protocol and knows nothing about sockets. This is the
//  glue: an SDK client, a `NetBattle`, and the eight lines between them — sign in,
//  queue, take the side and seed the server hands you, feed every peer frame to the
//  protocol, send everything it produces, and when the battle ends, store the tape
//  and report the outcome to the ladder.
//
//  It exists as a class rather than as a loop inside `main.cpp` for the reason
//  every operation in this project does: `--pvp` and `test_creature_pvp_live` must
//  be the same code. A protocol glued together twice is a protocol with two
//  behaviours, and the one under test would be the one nobody ships.
//
//  The ACTION used to be chosen by `choose` unconditionally, and the comment here said
//  "a player-driven one would take the action from a screen instead; everything else
//  here is unchanged, which is the point of the protocol being pure." Chapter 146 is
//  that screen, and the comment was right — `set_auto_play(false)` and one `act()` is
//  the whole difference. Everything below it is the same code, which is why `--pvp`
//  still plays the same match.
//
//  Non-blocking. `update()` is one tick: it pumps the client, drains the socket and
//  returns. Nothing here waits, because the same rule that forbids a `while(true)`
//  game loop forbids one in a network client.
// =============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "games/creatures/defs.hpp"
#include "games/creatures/netbattle.hpp"
#include "gbaas/gbaas.h"

namespace creature {

inline constexpr const char* kLadderBoard = "creature_elo";

class PvpClient {
public:
    enum class State : std::uint8_t {
        Idle = 0,
        SigningIn,   // waiting for a guest session
        Connecting,  // opening the realtime socket
        Queued,      // in matchmaking, waiting for an opponent
        Playing,     // matched; a battle is running
        Reporting,   // the battle ended; storing the tape and rating the match
        Done,
        Failed
    };

    // `transport` is the SDK's own unless a caller injects one. The WEBSOCKET is not
    // injectable — `gbaas::Client` builds its `Realtime` on first use with the SDK's
    // own ws transport and takes no parameter for it — so a test that wants a whole
    // match still needs a server on a port, which is what `test_creature_pvp_live`
    // does. This header claimed otherwise for nine chapters; the claim was about a
    // constructor parameter that did not exist.
    PvpClient(const Dex& d, gbaas::Config cfg, std::string board = kLadderBoard,
              std::unique_ptr<gbaas::ITransport> transport = nullptr);

    // Sign in, connect, queue — all driven by `update()`, none of it blocking.
    void start(const Party& mine);
    void update();

    // Give up: leave matchmaking and stop. Safe at any point — a player who taps
    // Cancel while the socket is still opening must not be left queued on a server
    // that will later match them with somebody who then waits for a peer that is not
    // coming. Returns to Idle; a match already in progress is NOT abandoned this way.
    void cancel();

    // ---- who picks the action (chapter 146) --------------------------------------
    // On by default: `--pvp` is headless and has nobody to ask. A scene turns it off
    // and calls `act()` from a button.
    void set_auto_play(bool on) { auto_play_ = on; }
    [[nodiscard]] bool auto_play() const { return auto_play_; }

    // True exactly when the protocol wants an action and this client will not invent
    // one. False while the peer's frame is still in flight, which is the state a
    // screen has to be able to tell apart from "your turn" — a menu that stays live
    // between turns lets a player queue an action the protocol has nowhere to put.
    [[nodiscard]] bool waiting_for_action() const;

    // Play one. Refused (returns false) unless `waiting_for_action()`, so a second tap
    // on the same button cannot send a second action for one turn.
    bool act(Action a);

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] const NetBattle&   net()      const { return net_; }
    [[nodiscard]] const std::string& problem()  const { return problem_; }
    [[nodiscard]] long long          user_id()  const { return user_id_; }
    [[nodiscard]] const std::string& room()     const { return room_; }
    [[nodiscard]] long long   opponent_id()   const { return opponent_id_; }
    [[nodiscard]] const std::string& opponent_name() const { return opponent_name_; }

    // Filled once the match is over and reported.
    [[nodiscard]] const std::string& tape_text() const { return tape_text_; }
    [[nodiscard]] long long replay_id()    const { return replay_id_; }
    [[nodiscard]] int       rating()       const { return rating_; }
    [[nodiscard]] int       rating_delta() const { return rating_delta_; }
    [[nodiscard]] bool      rating_applied() const { return rating_applied_; }

    // For a caller that wants to drive the SDK itself (a scene pumping one client
    // for several services at once).
    [[nodiscard]] gbaas::Client& client() { return client_; }

private:
    void fail(std::string why);
    void finish_match();

    const Dex&     dex_;
    gbaas::Client  client_;
    std::string    board_;
    NetBattle      net_;
    Party          mine_{};

    bool        auto_play_ = true;
    State       state_ = State::Idle;
    std::string problem_;
    long long   user_id_ = 0;
    std::string room_;
    long long   opponent_id_ = 0;
    std::string opponent_name_;

    std::string tape_text_;
    long long   replay_id_ = 0;
    int         rating_ = 0, rating_delta_ = 0;
    bool        rating_applied_ = false;
    bool        stored_ = false, reported_ = false;
    bool        store_done_ = false, report_done_ = false;
};

// The headless `--pvp` mode: sign in, queue, play one rated match, print what
// happened, exit. Returns a process exit code.
int run_pvp(const std::string& base_url, const std::string& api_key);

} // namespace creature
