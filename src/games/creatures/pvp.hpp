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
//  The ACTION is chosen by `choose` — this is a headless client, and there is
//  nobody to ask. A player-driven one would take the action from a screen instead;
//  everything else here is unchanged, which is the point of the protocol being pure.
//
//  Non-blocking. `update()` is one tick: it pumps the client, drains the socket and
//  returns. Nothing here waits, because the same rule that forbids a `while(true)`
//  game loop forbids one in a network client.
// =============================================================================
#pragma once

#include <cstdint>
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

    // `transport`/`ws` are the SDK's own unless a caller injects them, which is what
    // lets a test drive this without opening a socket to a port somebody else owns.
    PvpClient(const Dex& d, gbaas::Config cfg, std::string board = kLadderBoard);

    // Sign in, connect, queue — all driven by `update()`, none of it blocking.
    void start(const Party& mine);
    void update();

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
