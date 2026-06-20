// =============================================================================
//  games/chess/piece.hpp  —  pieces, colors, and square helpers
// =============================================================================
//  The chess CORE is plain C++ with NO dependency on the engine or SDL, so it can
//  be unit-tested and reused by both the GUI and TUI frontends. This header holds
//  the smallest vocabulary: what a piece is, and how we number squares.
//
//  Square numbering (mailbox 8x8): a single 0..63 index, a1 = 0 … h8 = 63.
//      index = rank * 8 + file        file = index % 8 (a=0..h=7)
//                                     rank = index / 8 (1=0..8=7)
// =============================================================================
#pragma once

#include <cstdint>

namespace chess {

enum class Color : uint8_t { White, Black };

enum class PieceType : uint8_t { None, Pawn, Knight, Bishop, Rook, Queen, King };

struct Piece {
    PieceType type  = PieceType::None;
    Color     color = Color::White;

    bool is_none() const { return type == PieceType::None; }
};

inline Color opposite(Color c) {
    return c == Color::White ? Color::Black : Color::White;
}

constexpr int kBoardSquares = 64;

inline int make_square(int file, int rank) { return rank * 8 + file; }
inline int file_of(int sq) { return sq % 8; }
inline int rank_of(int sq) { return sq / 8; }
inline bool on_board(int file, int rank) {
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

} // namespace chess
