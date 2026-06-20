// =============================================================================
//  games/chess/evaluate.hpp  —  static position evaluation
// =============================================================================
//  Turns a position into a single number (centipawns) the search maximizes.
//  Convention: the score is returned from the SIDE-TO-MOVE's point of view
//  (positive = good for whoever is to move), which pairs cleanly with negamax.
// =============================================================================
#pragma once

#include "games/chess/board.hpp"

namespace chess {

// Material + piece-square evaluation, in centipawns, from side-to-move's view.
int evaluate(const State& s);

} // namespace chess
