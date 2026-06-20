// =============================================================================
//  games/chess/board.hpp  —  full game State (the "position")
// =============================================================================
//  A chess position is more than where the pieces sit: the rules also depend on
//  whose turn it is, who may still castle, whether en passant is available, and
//  the move clocks. All of that is the State — exactly the six things a FEN string
//  encodes (see fen.hpp).
// =============================================================================
#pragma once

#include <array>
#include <string>

#include "games/chess/piece.hpp"

namespace chess {

struct State {
    std::array<Piece, 64> board{};        // a1=0 … h8=63 (default: all None)
    Color side_to_move = Color::White;

    // Castling rights (lost permanently once king/rook moves or rook is captured).
    bool white_kingside  = false;
    bool white_queenside = false;
    bool black_kingside  = false;
    bool black_queenside = false;

    int en_passant     = -1;  // square a pawn may capture onto this move, or -1
    int halfmove_clock = 0;   // plies since last capture/pawn move (50-move rule)
    int fullmove_number = 1;  // increments after Black moves

    Piece at(int sq) const { return board[static_cast<size_t>(sq)]; }
};

// The standard chess starting position.
State initial_state();

// A human-readable 8x8 dump (rank 8 at top), for debugging and the TUI.
std::string ascii_board(const State& s);

} // namespace chess
