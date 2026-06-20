// =============================================================================
//  games/chess/movegen.hpp  —  move generation, attacks, make/unmake, perft
// =============================================================================
//  The core of a chess engine. `generate_legal` produces every move the side to
//  move may legally play (the king may not be left in check); make/unmake apply
//  and reverse a move; perft counts leaf positions to a depth and is the gold-
//  standard correctness test for all of the above.
// =============================================================================
#pragma once

#include <vector>

#include "games/chess/board.hpp"
#include "games/chess/move.hpp"

namespace chess {

// Is `sq` attacked by any piece of color `by`? (Underlies check + castling.)
bool is_square_attacked(const State& s, int sq, Color by);

int  king_square(const State& s, Color c);     // -1 if no king (only in tests)
bool in_check(const State& s, Color c);

// Pseudo-legal = ignores leaving your own king in check. Legal = filtered.
void generate_pseudo_legal(const State& s, std::vector<Move>& out);
void generate_legal(const State& s, std::vector<Move>& out);

// Apply / reverse a move. unmake_move(s, make_move(s, m)) restores s exactly.
Undo make_move(State& s, const Move& m);
void unmake_move(State& s, const Undo& u);

// Count leaf nodes at the given depth (perft). The classic move-gen test.
long long perft(State& s, int depth);

} // namespace chess
