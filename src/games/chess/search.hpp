// =============================================================================
//  games/chess/search.hpp  —  the AI: minimax + alpha-beta search
// =============================================================================
//  Picks a move by searching ahead `depth` plies, assuming both sides play the
//  evaluation-maximizing move (negamax form of minimax) and pruning branches
//  that cannot affect the result (alpha-beta). Difficulty = search depth.
// =============================================================================
#pragma once

#include "games/chess/board.hpp"
#include "games/chess/move.hpp"

namespace chess {

enum class Difficulty { Easy = 2, Medium = 4, Hard = 6 };

struct SearchResult {
    Move      best;       // the chosen move
    int       score;      // centipawns from side-to-move's view (huge = mate)
    long long nodes;      // nodes visited (to show alpha-beta's effect)
};

SearchResult search(const State& s, int depth);

inline SearchResult search(const State& s, Difficulty d) {
    return search(s, static_cast<int>(d));
}

} // namespace chess
