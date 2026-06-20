// =============================================================================
//  tests/test_chess.cpp  —  chess core unit tests (dependency-free, CTest)
// =============================================================================
//  Step 1 coverage: FEN parse/format round-trips and start-position sanity.
//  (perft and rule tests are added in Step 2+.)
// =============================================================================
#include "games/chess/board.hpp"
#include "games/chess/fen.hpp"

#include <cstdio>
#include <string>

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

int main() {
    test_start_position();
    test_fen_roundtrip();
    test_rejects_garbage();

    if (g_failures == 0) std::printf("chess: all tests passed\n");
    else                 std::printf("chess: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
