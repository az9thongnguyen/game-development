// =============================================================================
//  games/chess/move.hpp  —  a move, and the info needed to undo it
// =============================================================================
//  A Move records from/to plus the special-case flags chess needs (promotion,
//  capture, double pawn push, en passant, castle). Undo captures everything
//  make_move changes that isn't recoverable from the board alone, so unmake_move
//  can restore the exact previous State — essential for a fast search (perft/AI).
// =============================================================================
#pragma once

#include <cstdint>

#include "games/chess/piece.hpp"

namespace chess {

struct Move {
    uint8_t   from = 0;
    uint8_t   to   = 0;
    PieceType promotion = PieceType::None;  // Queen/Rook/Bishop/Knight, or None
    bool      capture     = false;
    bool      double_push = false;          // pawn moved two squares
    bool      en_passant  = false;          // pawn captured en passant
    bool      castle      = false;          // king moved two squares
};

struct Undo {
    Move  move;
    Piece captured;            // piece removed by the move (None if quiet)
    int   captured_sq = -1;    // where it sat (== move.to, except en passant)
    // Position metadata to restore:
    bool  wk = false, wq = false, bk = false, bq = false;  // castling rights
    int   en_passant     = -1;
    int   halfmove_clock = 0;
    int   fullmove_number = 1;
};

} // namespace chess
