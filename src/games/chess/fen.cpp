// =============================================================================
//  games/chess/fen.cpp
// =============================================================================
#include "games/chess/fen.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

namespace chess {

namespace {

std::optional<PieceType> type_from_char(char lower) {
    switch (lower) {
        case 'p': return PieceType::Pawn;
        case 'n': return PieceType::Knight;
        case 'b': return PieceType::Bishop;
        case 'r': return PieceType::Rook;
        case 'q': return PieceType::Queen;
        case 'k': return PieceType::King;
        default:  return std::nullopt;
    }
}

char char_from_piece(Piece p) {
    char c = '?';
    switch (p.type) {
        case PieceType::Pawn:   c = 'p'; break;
        case PieceType::Knight: c = 'n'; break;
        case PieceType::Bishop: c = 'b'; break;
        case PieceType::Rook:   c = 'r'; break;
        case PieceType::Queen:  c = 'q'; break;
        case PieceType::King:   c = 'k'; break;
        case PieceType::None:   return '?';
    }
    if (p.color == Color::White) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return c;
}

} // namespace

std::optional<State> parse_fen(const std::string& fen) {
    std::vector<std::string> f;
    {
        std::istringstream iss(fen);
        std::string tok;
        while (iss >> tok) f.push_back(tok);
    }
    if (f.size() < 4) return std::nullopt;  // placement..ep required; clocks optional

    State s;  // board defaults to all None

    // ---- field 0: piece placement, from rank 8 down to rank 1 ----
    int rank = 7, file = 0;
    for (char c : f[0]) {
        if (c == '/') {
            if (file != 8) return std::nullopt;
            --rank;
            file = 0;
            if (rank < 0) return std::nullopt;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
            if (file > 8) return std::nullopt;
        } else {
            if (file >= 8 || rank < 0) return std::nullopt;
            const auto t = type_from_char(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (!t) return std::nullopt;
            Piece p;
            p.type  = *t;
            p.color = std::isupper(static_cast<unsigned char>(c)) ? Color::White : Color::Black;
            s.board[static_cast<size_t>(make_square(file, rank))] = p;
            ++file;
        }
    }
    if (rank != 0 || file != 8) return std::nullopt;  // must consume all 8 ranks

    // ---- field 1: side to move ----
    if (f[1] == "w") s.side_to_move = Color::White;
    else if (f[1] == "b") s.side_to_move = Color::Black;
    else return std::nullopt;

    // ---- field 2: castling rights ----
    if (f[2] != "-") {
        for (char c : f[2]) {
            switch (c) {
                case 'K': s.white_kingside  = true; break;
                case 'Q': s.white_queenside = true; break;
                case 'k': s.black_kingside  = true; break;
                case 'q': s.black_queenside = true; break;
                default:  return std::nullopt;
            }
        }
    }

    // ---- field 3: en-passant target ----
    if (f[3] == "-") {
        s.en_passant = -1;
    } else {
        if (f[3].size() != 2) return std::nullopt;
        const int ef = f[3][0] - 'a';
        const int er = f[3][1] - '1';
        if (!on_board(ef, er)) return std::nullopt;
        s.en_passant = make_square(ef, er);
    }

    // ---- fields 4/5: clocks (optional) ----
    s.halfmove_clock  = (f.size() > 4) ? std::atoi(f[4].c_str()) : 0;
    s.fullmove_number = (f.size() > 5) ? std::atoi(f[5].c_str()) : 1;

    return s;
}

std::string to_fen(const State& s) {
    std::string out;

    for (int rank = 7; rank >= 0; --rank) {
        int empty = 0;
        for (int file = 0; file < 8; ++file) {
            const Piece p = s.at(make_square(file, rank));
            if (p.is_none()) {
                ++empty;
                continue;
            }
            if (empty) { out += static_cast<char>('0' + empty); empty = 0; }
            out += char_from_piece(p);
        }
        if (empty) out += static_cast<char>('0' + empty);
        if (rank > 0) out += '/';
    }

    out += ' ';
    out += (s.side_to_move == Color::White ? 'w' : 'b');

    out += ' ';
    std::string cr;
    if (s.white_kingside)  cr += 'K';
    if (s.white_queenside) cr += 'Q';
    if (s.black_kingside)  cr += 'k';
    if (s.black_queenside) cr += 'q';
    out += cr.empty() ? "-" : cr;

    out += ' ';
    if (s.en_passant < 0) {
        out += '-';
    } else {
        out += static_cast<char>('a' + file_of(s.en_passant));
        out += static_cast<char>('1' + rank_of(s.en_passant));
    }

    out += ' ';
    out += std::to_string(s.halfmove_clock);
    out += ' ';
    out += std::to_string(s.fullmove_number);
    return out;
}

} // namespace chess
