// =============================================================================
//  games/chess/game.cpp
// =============================================================================
#include "games/chess/game.hpp"

#include <cctype>

#include "games/chess/movegen.hpp"

namespace chess {

std::string square_to_string(int sq) {
    std::string s;
    s += static_cast<char>('a' + file_of(sq));
    s += static_cast<char>('1' + rank_of(sq));
    return s;
}

std::string move_to_string(const Move& m) {
    std::string s = square_to_string(m.from) + square_to_string(m.to);
    if (m.promotion != PieceType::None) {
        char c = 'q';
        switch (m.promotion) {
            case PieceType::Queen:  c = 'q'; break;
            case PieceType::Rook:   c = 'r'; break;
            case PieceType::Bishop: c = 'b'; break;
            case PieceType::Knight: c = 'n'; break;
            default: break;
        }
        s += c;
    }
    return s;
}

std::optional<Move> find_move(const State& s, const std::string& coord) {
    std::string c;
    for (char ch : coord) c += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

    std::vector<Move> moves;
    generate_legal(s, moves);
    for (const Move& m : moves) {
        if (move_to_string(m) == c) return m;
    }
    return std::nullopt;
}

std::vector<Move> Game::legal_moves() const {
    std::vector<Move> v;
    generate_legal(state, v);
    return v;
}

bool Game::is_check() const {
    return in_check(state, state.side_to_move);
}

Result Game::result() const {
    std::vector<Move> v;
    generate_legal(state, v);
    if (v.empty()) {
        return in_check(state, state.side_to_move) ? Result::Checkmate : Result::Stalemate;
    }
    if (state.halfmove_clock >= 100) {  // 50 full moves = 100 plies
        return Result::FiftyMoveDraw;
    }
    return Result::Ongoing;
}

Color Game::winner() const {
    // On checkmate, the side NOT to move delivered mate.
    return opposite(state.side_to_move);
}

bool Game::play(const Move& m) {
    // Accept only a legal move; match by from/to/promotion so the fully-flagged
    // legal move (with castle/en-passant flags) is the one actually applied.
    std::vector<Move> v;
    generate_legal(state, v);
    for (const Move& lm : v) {
        if (lm.from == m.from && lm.to == m.to && lm.promotion == m.promotion) {
            make_move(state, lm);
            return true;
        }
    }
    return false;
}

} // namespace chess
