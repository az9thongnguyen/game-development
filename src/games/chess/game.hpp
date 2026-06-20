// =============================================================================
//  games/chess/game.hpp  —  the Game controller (facade over the rules)
// =============================================================================
//  A thin, UI-agnostic facade the frontends and AI drive: it owns the current
//  State, lists legal moves, applies a move, detects game-over, and converts
//  moves to/from coordinate notation ("e2e4", "e7e8q"). It hides make/unmake
//  behind a simple play()/result() API.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "games/chess/board.hpp"
#include "games/chess/move.hpp"

namespace chess {

enum class Result { Ongoing, Checkmate, Stalemate, FiftyMoveDraw };

// Coordinate (long-algebraic) notation helpers.
std::string         square_to_string(int sq);            // 12 -> "e2"
std::string         move_to_string(const Move& m);       // -> "e2e4" / "e7e8q"
std::optional<Move> find_move(const State& s, const std::string& coord);  // among legal

struct Game {
    State state = initial_state();

    std::vector<Move> legal_moves() const;
    bool   is_check() const;
    Result result() const;
    Color  winner() const;        // meaningful when result()==Checkmate
    bool   play(const Move& m);   // applies if legal (matched by from/to/promo)
    void   reset() { state = initial_state(); }
};

} // namespace chess
