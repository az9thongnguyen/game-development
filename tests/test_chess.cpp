// =============================================================================
//  tests/test_chess.cpp  —  chess core unit tests (dependency-free, CTest)
// =============================================================================
//  Step 1 coverage: FEN parse/format round-trips and start-position sanity.
//  (perft and rule tests are added in Step 2+.)
// =============================================================================
#include "games/chess/board.hpp"
#include "games/chess/fen.hpp"
#include "games/chess/movegen.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace chess;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static int count_pieces(const State& s) {
    int n = 0;
    for (const Piece& p : s.board) {
        if (!p.is_none()) ++n;
    }
    return n;
}

static void test_start_position() {
    auto parsed = parse_fen(kStartFEN);
    CHECK(parsed.has_value());
    if (!parsed) return;
    const State& s = *parsed;

    CHECK(count_pieces(s) == 32);
    CHECK(s.side_to_move == Color::White);
    CHECK(s.white_kingside && s.white_queenside && s.black_kingside && s.black_queenside);
    CHECK(s.en_passant == -1);
    CHECK(s.halfmove_clock == 0);
    CHECK(s.fullmove_number == 1);

    // Spot-check a few squares.
    CHECK(s.at(make_square(0, 0)).type == PieceType::Rook);   // a1
    CHECK(s.at(make_square(0, 0)).color == Color::White);
    CHECK(s.at(make_square(4, 0)).type == PieceType::King);   // e1
    CHECK(s.at(make_square(3, 7)).type == PieceType::Queen);  // d8
    CHECK(s.at(make_square(4, 7)).color == Color::Black);     // e8
    CHECK(s.at(make_square(4, 3)).is_none());                 // e4 empty
}

static void test_fen_roundtrip() {
    CHECK(to_fen(*parse_fen(kStartFEN)) == std::string(kStartFEN));
    CHECK(to_fen(initial_state()) == std::string(kStartFEN));

    const char* positions[] = {
        "r1bqk2r/pppp1ppp/2n2n2/2b1p3/2B1P3/3P1N2/PPP2PPP/RNBQK2R w KQkq - 4 5",
        "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2",  // en passant
        "8/8/8/4k3/8/4K3/8/8 b - - 10 30",                                 // sparse, no castling
    };
    for (const char* fen : positions) {
        auto s = parse_fen(fen);
        CHECK(s.has_value());
        if (s) CHECK(to_fen(*s) == std::string(fen));
    }
}

static void test_rejects_garbage() {
    CHECK(!parse_fen("not a fen").has_value());
    CHECK(!parse_fen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1").has_value());
}

static long long run_perft(const char* fen, int depth) {
    auto s = parse_fen(fen);
    if (!s) { ++g_failures; std::printf("FAIL bad fen %s\n", fen); return -1; }
    return perft(*s, depth);
}

static void test_perft() {
    // Starting position — the canonical reference counts.
    CHECK(run_perft(kStartFEN, 1) == 20);
    CHECK(run_perft(kStartFEN, 2) == 400);
    CHECK(run_perft(kStartFEN, 3) == 8902);
    CHECK(run_perft(kStartFEN, 4) == 197281);

    // "Kiwipete": dense middlegame exercising castling, en passant, pins, promos.
    const char* kiwipete = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    CHECK(run_perft(kiwipete, 1) == 48);
    CHECK(run_perft(kiwipete, 2) == 2039);
    CHECK(run_perft(kiwipete, 3) == 97862);

    // En-passant / promotion heavy endgame (CPW perft position 3).
    const char* ep = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    CHECK(run_perft(ep, 1) == 14);
    CHECK(run_perft(ep, 2) == 191);
    CHECK(run_perft(ep, 3) == 2812);
}

static void test_end_states() {
    // Fool's mate (1.f3 e5 2.g4 Qh4#): White to move, checkmated → 0 legal moves.
    auto mate = parse_fen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    CHECK(mate.has_value());
    if (mate) {
        std::vector<Move> mv; generate_legal(*mate, mv);
        CHECK(mv.empty());
        CHECK(in_check(*mate, Color::White));
    }
    // Stalemate: Black to move, no legal move, NOT in check.
    auto stale = parse_fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(stale.has_value());
    if (stale) {
        std::vector<Move> mv; generate_legal(*stale, mv);
        CHECK(mv.empty());
        CHECK(!in_check(*stale, Color::Black));
    }
}

int main() {
    test_start_position();
    test_fen_roundtrip();
    test_rejects_garbage();
    test_perft();
    test_end_states();

    if (g_failures == 0) std::printf("chess: all tests passed\n");
    else                 std::printf("chess: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
