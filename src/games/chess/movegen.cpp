// =============================================================================
//  games/chess/movegen.cpp
// =============================================================================
#include "games/chess/movegen.hpp"

namespace chess {

namespace {

// Direction tables as (file_delta, rank_delta). Mailbox needs file/rank bounds
// checks (not raw index arithmetic) to avoid wrapping across board edges.
const int KNIGHT[8][2] = {{1,2},{2,1},{2,-1},{1,-2},{-1,-2},{-2,-1},{-2,1},{-1,2}};
const int KING_DIR[8][2] = {{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
const int BISHOP_DIR[4][2] = {{1,1},{1,-1},{-1,1},{-1,-1}};
const int ROOK_DIR[4][2]   = {{1,0},{-1,0},{0,1},{0,-1}};

void slide_moves(const State& s, int sq, const int dirs[][2], int ndir,
                 Color them, std::vector<Move>& out) {
    const int f = file_of(sq), r = rank_of(sq);
    for (int d = 0; d < ndir; ++d) {
        int nf = f + dirs[d][0], nr = r + dirs[d][1];
        while (on_board(nf, nr)) {
            const int to = make_square(nf, nr);
            const Piece t = s.board[static_cast<size_t>(to)];
            if (t.is_none()) {
                Move m; m.from = static_cast<uint8_t>(sq); m.to = static_cast<uint8_t>(to);
                out.push_back(m);
            } else {
                if (t.color == them) {
                    Move m; m.from = static_cast<uint8_t>(sq); m.to = static_cast<uint8_t>(to);
                    m.capture = true; out.push_back(m);
                }
                break;  // blocked
            }
            nf += dirs[d][0]; nr += dirs[d][1];
        }
    }
}

void add_simple(int from, int to, bool capture, std::vector<Move>& out) {
    Move m; m.from = static_cast<uint8_t>(from); m.to = static_cast<uint8_t>(to);
    m.capture = capture; out.push_back(m);
}

void generate_castling(const State& s, int ksq, Color us, std::vector<Move>& out) {
    const Color them = opposite(us);
    const int rank = (us == Color::White) ? 0 : 7;
    if (ksq != make_square(4, rank)) return;                 // king must be home
    if (is_square_attacked(s, ksq, them)) return;            // can't castle out of check

    const bool kside = (us == Color::White) ? s.white_kingside  : s.black_kingside;
    const bool qside = (us == Color::White) ? s.white_queenside : s.black_queenside;

    if (kside) {
        const int f1 = make_square(5, rank), g1 = make_square(6, rank);
        if (s.board[static_cast<size_t>(f1)].is_none() &&
            s.board[static_cast<size_t>(g1)].is_none() &&
            !is_square_attacked(s, f1, them) && !is_square_attacked(s, g1, them)) {
            Move m; m.from = static_cast<uint8_t>(ksq); m.to = static_cast<uint8_t>(g1);
            m.castle = true; out.push_back(m);
        }
    }
    if (qside) {
        const int d1 = make_square(3, rank), c1 = make_square(2, rank), b1 = make_square(1, rank);
        if (s.board[static_cast<size_t>(d1)].is_none() &&
            s.board[static_cast<size_t>(c1)].is_none() &&
            s.board[static_cast<size_t>(b1)].is_none() &&
            !is_square_attacked(s, d1, them) && !is_square_attacked(s, c1, them)) {
            Move m; m.from = static_cast<uint8_t>(ksq); m.to = static_cast<uint8_t>(c1);
            m.castle = true; out.push_back(m);
        }
    }
}

} // namespace

bool is_square_attacked(const State& s, int sq, Color by) {
    const int f = file_of(sq), r = rank_of(sq);

    // Pawns: a `by` pawn that attacks sq sits one rank "behind" sq (relative to
    // its own forward direction), on an adjacent file.
    const int pawn_dir = (by == Color::White) ? +1 : -1;
    for (int df : {-1, 1}) {
        const int pf = f + df, pr = r - pawn_dir;
        if (on_board(pf, pr)) {
            const Piece p = s.board[static_cast<size_t>(make_square(pf, pr))];
            if (!p.is_none() && p.color == by && p.type == PieceType::Pawn) return true;
        }
    }
    // Knights
    for (auto& o : KNIGHT) {
        const int nf = f + o[0], nr = r + o[1];
        if (!on_board(nf, nr)) continue;
        const Piece p = s.board[static_cast<size_t>(make_square(nf, nr))];
        if (!p.is_none() && p.color == by && p.type == PieceType::Knight) return true;
    }
    // King
    for (auto& o : KING_DIR) {
        const int nf = f + o[0], nr = r + o[1];
        if (!on_board(nf, nr)) continue;
        const Piece p = s.board[static_cast<size_t>(make_square(nf, nr))];
        if (!p.is_none() && p.color == by && p.type == PieceType::King) return true;
    }
    // Bishop / Queen (diagonals)
    for (auto& o : BISHOP_DIR) {
        int nf = f + o[0], nr = r + o[1];
        while (on_board(nf, nr)) {
            const Piece p = s.board[static_cast<size_t>(make_square(nf, nr))];
            if (!p.is_none()) {
                if (p.color == by && (p.type == PieceType::Bishop || p.type == PieceType::Queen))
                    return true;
                break;
            }
            nf += o[0]; nr += o[1];
        }
    }
    // Rook / Queen (orthogonals)
    for (auto& o : ROOK_DIR) {
        int nf = f + o[0], nr = r + o[1];
        while (on_board(nf, nr)) {
            const Piece p = s.board[static_cast<size_t>(make_square(nf, nr))];
            if (!p.is_none()) {
                if (p.color == by && (p.type == PieceType::Rook || p.type == PieceType::Queen))
                    return true;
                break;
            }
            nf += o[0]; nr += o[1];
        }
    }
    return false;
}

int king_square(const State& s, Color c) {
    for (int sq = 0; sq < 64; ++sq) {
        const Piece p = s.board[static_cast<size_t>(sq)];
        if (!p.is_none() && p.color == c && p.type == PieceType::King) return sq;
    }
    return -1;
}

bool in_check(const State& s, Color c) {
    const int k = king_square(s, c);
    return k >= 0 && is_square_attacked(s, k, opposite(c));
}

void generate_pseudo_legal(const State& s, std::vector<Move>& out) {
    const Color us = s.side_to_move;
    const Color them = opposite(us);
    const int forward    = (us == Color::White) ? +1 : -1;
    const int start_rank = (us == Color::White) ? 1 : 6;
    const int promo_rank = (us == Color::White) ? 7 : 0;
    const PieceType promos[4] = {PieceType::Queen, PieceType::Rook,
                                 PieceType::Bishop, PieceType::Knight};

    for (int sq = 0; sq < 64; ++sq) {
        const Piece p = s.board[static_cast<size_t>(sq)];
        if (p.is_none() || p.color != us) continue;
        const int f = file_of(sq), r = rank_of(sq);

        switch (p.type) {
            case PieceType::Pawn: {
                const int nr = r + forward;
                if (nr >= 0 && nr < 8) {
                    const int to = make_square(f, nr);
                    if (s.board[static_cast<size_t>(to)].is_none()) {
                        if (nr == promo_rank) {
                            for (PieceType pt : promos) {
                                Move m; m.from = static_cast<uint8_t>(sq); m.to = static_cast<uint8_t>(to);
                                m.promotion = pt; out.push_back(m);
                            }
                        } else {
                            add_simple(sq, to, false, out);
                            if (r == start_rank) {
                                const int to2 = make_square(f, r + 2 * forward);
                                if (s.board[static_cast<size_t>(to2)].is_none()) {
                                    Move m; m.from = static_cast<uint8_t>(sq); m.to = static_cast<uint8_t>(to2);
                                    m.double_push = true; out.push_back(m);
                                }
                            }
                        }
                    }
                }
                for (int df : {-1, 1}) {
                    const int cf = f + df, cr = r + forward;
                    if (!on_board(cf, cr)) continue;
                    const int to = make_square(cf, cr);
                    const Piece t = s.board[static_cast<size_t>(to)];
                    if (!t.is_none() && t.color == them) {
                        if (cr == promo_rank) {
                            for (PieceType pt : promos) {
                                Move m; m.from = static_cast<uint8_t>(sq); m.to = static_cast<uint8_t>(to);
                                m.capture = true; m.promotion = pt; out.push_back(m);
                            }
                        } else {
                            add_simple(sq, to, true, out);
                        }
                    } else if (s.en_passant != -1 && to == s.en_passant) {
                        Move m; m.from = static_cast<uint8_t>(sq); m.to = static_cast<uint8_t>(to);
                        m.capture = true; m.en_passant = true; out.push_back(m);
                    }
                }
                break;
            }
            case PieceType::Knight:
                for (auto& o : KNIGHT) {
                    const int nf = f + o[0], nr2 = r + o[1];
                    if (!on_board(nf, nr2)) continue;
                    const int to = make_square(nf, nr2);
                    const Piece t = s.board[static_cast<size_t>(to)];
                    if (t.is_none()) add_simple(sq, to, false, out);
                    else if (t.color == them) add_simple(sq, to, true, out);
                }
                break;
            case PieceType::King:
                for (auto& o : KING_DIR) {
                    const int nf = f + o[0], nr2 = r + o[1];
                    if (!on_board(nf, nr2)) continue;
                    const int to = make_square(nf, nr2);
                    const Piece t = s.board[static_cast<size_t>(to)];
                    if (t.is_none()) add_simple(sq, to, false, out);
                    else if (t.color == them) add_simple(sq, to, true, out);
                }
                generate_castling(s, sq, us, out);
                break;
            case PieceType::Bishop: slide_moves(s, sq, BISHOP_DIR, 4, them, out); break;
            case PieceType::Rook:   slide_moves(s, sq, ROOK_DIR,   4, them, out); break;
            case PieceType::Queen:
                slide_moves(s, sq, BISHOP_DIR, 4, them, out);
                slide_moves(s, sq, ROOK_DIR,   4, them, out);
                break;
            case PieceType::None: break;
        }
    }
}

void generate_legal(const State& s, std::vector<Move>& out) {
    std::vector<Move> pseudo;
    generate_pseudo_legal(s, pseudo);
    const Color us = s.side_to_move;
    for (const Move& m : pseudo) {
        State c = s;                 // fresh copy: legality doesn't depend on unmake
        (void)make_move(c, m);
        const int k = king_square(c, us);
        if (k >= 0 && !is_square_attacked(c, k, opposite(us))) {
            out.push_back(m);
        }
    }
}

Undo make_move(State& s, const Move& m) {
    Undo u;
    u.move = m;
    u.wk = s.white_kingside; u.wq = s.white_queenside;
    u.bk = s.black_kingside; u.bq = s.black_queenside;
    u.en_passant = s.en_passant;
    u.halfmove_clock = s.halfmove_clock;
    u.fullmove_number = s.fullmove_number;

    const Piece moving = s.board[m.from];
    const Color us = moving.color;
    const bool resets_clock = (moving.type == PieceType::Pawn) || m.capture;

    // Remove the captured piece (en passant captures behind the target square).
    if (m.en_passant) {
        const int cap_sq = make_square(file_of(m.to), rank_of(m.from));
        u.captured = s.board[static_cast<size_t>(cap_sq)];
        u.captured_sq = cap_sq;
        s.board[static_cast<size_t>(cap_sq)] = Piece{};
    } else if (m.capture) {
        u.captured = s.board[m.to];
        u.captured_sq = m.to;
    }

    // Move the piece (and promote if needed).
    s.board[m.to] = moving;
    s.board[m.from] = Piece{};
    if (m.promotion != PieceType::None) {
        s.board[m.to].type = m.promotion;
    }

    // Castling moves the rook too.
    if (m.castle) {
        const int rank = rank_of(m.from);
        if (file_of(m.to) == 6) {  // kingside: h-rook -> f
            const int rf = make_square(7, rank), rt = make_square(5, rank);
            s.board[static_cast<size_t>(rt)] = s.board[static_cast<size_t>(rf)];
            s.board[static_cast<size_t>(rf)] = Piece{};
        } else {                   // queenside: a-rook -> d
            const int rf = make_square(0, rank), rt = make_square(3, rank);
            s.board[static_cast<size_t>(rt)] = s.board[static_cast<size_t>(rf)];
            s.board[static_cast<size_t>(rf)] = Piece{};
        }
    }

    // Update castling rights.
    if (moving.type == PieceType::King) {
        if (us == Color::White) { s.white_kingside = false; s.white_queenside = false; }
        else                    { s.black_kingside = false; s.black_queenside = false; }
    }
    auto clear_rook_rights = [&](int sq) {
        if      (sq == make_square(0, 0)) s.white_queenside = false;
        else if (sq == make_square(7, 0)) s.white_kingside  = false;
        else if (sq == make_square(0, 7)) s.black_queenside = false;
        else if (sq == make_square(7, 7)) s.black_kingside  = false;
    };
    if (moving.type == PieceType::Rook) clear_rook_rights(m.from);
    if (u.captured.type == PieceType::Rook) clear_rook_rights(u.captured_sq);

    // En-passant target for next move (only after a double push).
    s.en_passant = -1;
    if (m.double_push) {
        s.en_passant = make_square(file_of(m.from), rank_of(m.from) + (us == Color::White ? +1 : -1));
    }

    s.halfmove_clock = resets_clock ? 0 : s.halfmove_clock + 1;
    if (us == Color::Black) ++s.fullmove_number;
    s.side_to_move = opposite(us);
    return u;
}

void unmake_move(State& s, const Undo& u) {
    const Move& m = u.move;
    s.side_to_move = opposite(s.side_to_move);  // back to the mover

    s.white_kingside = u.wk; s.white_queenside = u.wq;
    s.black_kingside = u.bk; s.black_queenside = u.bq;
    s.en_passant = u.en_passant;
    s.halfmove_clock = u.halfmove_clock;
    s.fullmove_number = u.fullmove_number;

    Piece moved = s.board[m.to];
    if (m.promotion != PieceType::None) moved.type = PieceType::Pawn;  // demote
    s.board[m.from] = moved;
    s.board[m.to] = Piece{};

    if (u.captured.type != PieceType::None) {
        s.board[static_cast<size_t>(u.captured_sq)] = u.captured;
    }

    if (m.castle) {
        const int rank = rank_of(m.from);
        if (file_of(m.to) == 6) {  // undo kingside
            const int rf = make_square(7, rank), rt = make_square(5, rank);
            s.board[static_cast<size_t>(rf)] = s.board[static_cast<size_t>(rt)];
            s.board[static_cast<size_t>(rt)] = Piece{};
        } else {                   // undo queenside
            const int rf = make_square(0, rank), rt = make_square(3, rank);
            s.board[static_cast<size_t>(rf)] = s.board[static_cast<size_t>(rt)];
            s.board[static_cast<size_t>(rt)] = Piece{};
        }
    }
}

long long perft(State& s, int depth) {
    if (depth == 0) return 1;
    std::vector<Move> moves;
    generate_legal(s, moves);
    if (depth == 1) return static_cast<long long>(moves.size());
    long long nodes = 0;
    for (const Move& m : moves) {
        const Undo u = make_move(s, m);
        nodes += perft(s, depth - 1);
        unmake_move(s, u);
    }
    return nodes;
}

} // namespace chess
