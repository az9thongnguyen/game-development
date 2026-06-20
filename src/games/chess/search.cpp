// =============================================================================
//  games/chess/search.cpp
// =============================================================================
#include "games/chess/search.hpp"

#include <algorithm>
#include <vector>

#include "games/chess/evaluate.hpp"
#include "games/chess/movegen.hpp"

namespace chess {
namespace {

constexpr int INF  = 1000000;
constexpr int MATE = 100000;

int piece_value(PieceType t) {
    switch (t) {
        case PieceType::Pawn:   return 100;
        case PieceType::Knight: return 320;
        case PieceType::Bishop: return 330;
        case PieceType::Rook:   return 500;
        case PieceType::Queen:  return 900;
        default:                return 0;
    }
}

// Order captures first (most-valuable-victim, least-valuable-attacker) so good
// moves are tried early — that is what makes alpha-beta prune effectively.
void order_moves(const State& s, std::vector<Move>& moves) {
    auto key = [&](const Move& m) {
        int k = 0;
        if (m.capture) {
            const int victim = m.en_passant ? 100 : piece_value(s.board[m.to].type);
            k += 10000 + victim * 10 - piece_value(s.board[m.from].type);
        }
        if (m.promotion != PieceType::None) k += 900 + piece_value(m.promotion);
        return k;
    };
    std::stable_sort(moves.begin(), moves.end(),
                     [&](const Move& a, const Move& b) { return key(a) > key(b); });
}

struct Searcher {
    long long nodes = 0;

    int negamax(State& s, int depth, int ply, int alpha, int beta) {
        ++nodes;
        if (depth == 0) return evaluate(s);

        std::vector<Move> moves;
        generate_legal(s, moves);
        if (moves.empty()) {
            // Checkmate (prefer faster mates via the -ply term) or stalemate.
            return in_check(s, s.side_to_move) ? -(MATE - ply) : 0;
        }
        order_moves(s, moves);

        int best = -INF;
        for (const Move& m : moves) {
            const Undo u = make_move(s, m);
            const int sc = -negamax(s, depth - 1, ply + 1, -beta, -alpha);
            unmake_move(s, u);
            if (sc > best) best = sc;
            if (best > alpha) alpha = best;
            if (alpha >= beta) break;  // beta cutoff: opponent won't allow this
        }
        return best;
    }
};

} // namespace

SearchResult search(const State& s0, int depth) {
    State s = s0;
    Searcher se;

    std::vector<Move> moves;
    generate_legal(s, moves);

    SearchResult res;
    res.best = Move{};
    res.score = -INF;
    res.nodes = 0;
    if (moves.empty()) {
        res.score = in_check(s, s.side_to_move) ? -MATE : 0;  // mated or stalemate
        return res;
    }
    order_moves(s, moves);

    int alpha = -INF;
    const int beta = INF;
    for (const Move& m : moves) {
        const Undo u = make_move(s, m);
        const int sc = -se.negamax(s, depth - 1, 1, -beta, -alpha);
        unmake_move(s, u);
        if (sc > res.score) { res.score = sc; res.best = m; }
        if (res.score > alpha) alpha = res.score;
    }
    res.nodes = se.nodes;
    return res;
}

} // namespace chess
