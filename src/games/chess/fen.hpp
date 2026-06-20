// =============================================================================
//  games/chess/fen.hpp  —  Forsyth–Edwards Notation (parse + format)
// =============================================================================
//  FEN is the standard one-line text encoding of a position, e.g. the start:
//
//    rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
//    |---- piece placement (rank 8 -> 1) ----| | |    |  |  '- fullmove
//                                              | |    |  '- halfmove clock
//                                              | |    '- en-passant target ('-' none)
//                                              | '- castling rights ('-' none)
//                                              '- side to move (w/b)
//
//  We use FEN to set up positions, build test cases (perft), and (later) save/load.
// =============================================================================
#pragma once

#include <optional>
#include <string>

#include "games/chess/board.hpp"

namespace chess {

inline constexpr const char* kStartFEN =
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

// Parse a FEN string into a State. Returns nullopt if it is malformed.
std::optional<State> parse_fen(const std::string& fen);

// Format a State back into a FEN string.
std::string to_fen(const State& s);

} // namespace chess
