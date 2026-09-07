// =============================================================================
//  games/creatures/netbattle.hpp  —  one battle, two machines, four bytes a turn
// =============================================================================
//  This is what chapters 136 and 138 were for.
//
//  Two players fight one battle. Neither sends a world, a health bar or a damage
//  number: each sends the ACTION it chose — a kind and an index, four bytes — and
//  both compute the whole turn from it. That is only possible because `step` is
//  integer arithmetic over state the two sides already share, and it is only SAFE
//  because both then send `hash(battle)` and compare. A protocol that exchanged
//  outcomes would have to trust them; this one detects the disagreement instead.
//
//  Three things are deliberately NOT chosen by a player:
//
//   * WHICH SIDE you are, and THE SEED — both arrive in the server's `matched`
//     event. A seed mixed from two client contributions sounds fairer and is not:
//     whoever sends second can grind their half. The server picking it is the only
//     version that needs no commit-reveal.
//   * WHAT YOUR CREATURES ARE. The wire carries `species:level`, and both peers
//     call `make(d, species, level)` — so a peer can lie about which creatures it
//     brings, and cannot lie about what they are. Same rule as the save and the
//     replay: stats are derived, never transmitted.
//
//  WHAT THIS IS NOT: a referee. There is no server-side authority here — two peers
//  agree or they do not. It detects a desync; it cannot stop two clients that have
//  both been modified the same way. That is a deliberate limitation of a
//  peer-to-peer ladder, and it is the reason `verify` and a stored replay exist:
//  the tape a match produces is checkable afterwards by something neither played.
//
//  PURE: no sockets, no SDK, no clock. Frames come in as strings and go out as
//  strings; who carries them is the caller's problem. That is what lets a test play
//  a whole match by handing two of these objects each other's frames.
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "games/creatures/battle.hpp"
#include "games/creatures/defs.hpp"
#include "games/creatures/replay.hpp"

namespace creature {

inline constexpr int kNetVersion = 1;

// How far ahead of the turn being resolved a peer's frame may refer. A peer can be
// at most one turn ahead of us (it acted, we have not); anything beyond that is
// either a broken peer or an attempt to make us allocate.
inline constexpr int kNetLookahead = 2;

enum class NetPhase : std::uint8_t {
    Idle = 0,    // begin() has not been called
    Handshake,   // parties are being exchanged
    MyTurn,      // both parties are in and this side owes an action
    Waiting,     // acted; waiting for the peer's action
    Over,        // the battle finished
    Desync,      // the two sides computed different states — see desync_turn
    Refused      // the peer sent something this build will not honour
};

class NetBattle {
public:
    // From the server's `matched` event. `mine` is the party this side brings; only
    // species and level travel, so both peers rebuild identical creatures.
    void begin(const Dex& d, int my_side, std::uint64_t seed, const Party& mine);

    // One frame from the peer. False means the frame was refused and `phase` is now
    // Refused (or Desync) — the match is over either way.
    bool on_frame(const Dex& d, const std::string& frame);

    // This side's action for the current turn. Does nothing unless phase == MyTurn.
    void act(const Dex& d, Action a);

    // Frames waiting to be sent, oldest first. Drains the queue.
    std::vector<std::string> drain();

    [[nodiscard]] NetPhase phase() const { return phase_; }
    [[nodiscard]] int      side()  const { return side_; }
    [[nodiscard]] const Battle& battle() const { return battle_; }
    [[nodiscard]] const Replay& tape()   const { return tape_; }
    [[nodiscard]] const std::vector<Event>& log() const { return log_; }
    [[nodiscard]] const std::string& problem() const { return problem_; }

    // Set when phase == Desync: the turn the two sides first disagreed on, and the
    // two hashes. Same shape as `Verdict`, and for the same reason — the END of a
    // desynced match is two unrelated states, and only the first divergence is a
    // thing anyone can debug.
    [[nodiscard]] int           desync_turn() const { return desync_turn_; }
    [[nodiscard]] std::uint64_t mine_hash()   const { return mine_hash_; }
    [[nodiscard]] std::uint64_t their_hash()  const { return their_hash_; }

    // -1 while the match is running or on a draw; otherwise the winning SIDE.
    [[nodiscard]] int winner() const { return battle_.over ? battle_.winner : -1; }
    // Did this side win? Meaningful once phase == Over.
    [[nodiscard]] bool won() const { return battle_.over && battle_.winner == side_; }

private:
    bool refuse(std::string why);
    void try_build(const Dex& d);
    void try_resolve(const Dex& d);
    void check_hash(int turn);

    NetPhase      phase_ = NetPhase::Idle;
    int           side_  = 0;
    std::uint64_t seed_  = 0;
    Party         mine_, theirs_;
    bool          have_theirs_ = false;
    Battle        battle_;
    Replay        tape_;
    std::vector<Event> log_;

    int                   turn_ = 0;          // turns resolved so far
    bool                  acted_ = false;     // this side's action for turn_ + 1
    Action                my_action_{};
    std::map<int, Action>        peer_action_;
    std::map<int, std::uint64_t> peer_hash_;

    std::vector<std::string> outbox_;
    std::string   problem_;
    int           desync_turn_ = 0;
    std::uint64_t mine_hash_ = 0, their_hash_ = 0;
};

// The one place the party format is written, so the sender and the reader cannot
// drift. `species:level` pairs, space separated, at most kPartySize of them.
std::string write_party(const Party& p);
bool        read_party(const Dex& d, const std::string& text, Party& out);

} // namespace creature
