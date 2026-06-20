// =============================================================================
//  games/chess/board.cpp
// =============================================================================
#include "games/chess/board.hpp"

#include "games/chess/fen.hpp"

namespace chess {

State initial_state() {
    // Defined via FEN so there is one source of truth (and it exercises the parser).
    if (auto s = parse_fen(kStartFEN)) {
        return *s;
    }
    return State{};  // unreachable: kStartFEN is valid
}

namespace {
char glyph_of(Piece p) {
    char c = '.';
    switch (p.type) {
        case PieceType::Pawn:   c = 'p'; break;
        case PieceType::Knight: c = 'n'; break;
        case PieceType::Bishop: c = 'b'; break;
        case PieceType::Rook:   c = 'r'; break;
        case PieceType::Queen:  c = 'q'; break;
        case PieceType::King:   c = 'k'; break;
        case PieceType::None:   return '.';
    }
    if (p.color == Color::White) c = static_cast<char>(c - 'a' + 'A');  // uppercase
    return c;
}
} // namespace

std::string ascii_board(const State& s) {
    std::string out;
    for (int rank = 7; rank >= 0; --rank) {
        out += static_cast<char>('1' + rank);
        out += ' ';
        for (int file = 0; file < 8; ++file) {
            out += glyph_of(s.at(make_square(file, rank)));
            out += ' ';
        }
        out += '\n';
    }
    out += "  a b c d e f g h\n";
    return out;
}

} // namespace chess
