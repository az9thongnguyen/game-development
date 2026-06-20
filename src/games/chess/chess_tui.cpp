// =============================================================================
//  games/chess/chess_tui.cpp
// =============================================================================
#include "games/chess/chess_tui.hpp"

#include <iostream>
#include <string>

#include "games/chess/board.hpp"
#include "games/chess/game.hpp"
#include "games/chess/search.hpp"

namespace chess {

int run_tui(bool vs_ai, int ai_depth) {
    Game g;
    std::cout << "Hand-engine chess (TUI)\n"
              << (vs_ai ? "Human (White) vs AI (Black)\n" : "Human vs Human\n")
              << "Enter moves as coordinates, e.g. e2e4 or e7e8q.  'quit' to exit.\n";

    while (g.result() == Result::Ongoing) {
        std::cout << '\n' << ascii_board(g.state);
        const bool white = (g.state.side_to_move == Color::White);
        std::cout << (white ? "White" : "Black") << " to move"
                  << (g.is_check() ? "  (check)" : "") << '\n';

        const bool ai_turn = vs_ai && !white;  // AI is Black
        if (ai_turn) {
            const SearchResult r = search(g.state, ai_depth);
            std::cout << "AI plays " << move_to_string(r.best) << '\n';
            g.play(r.best);
        } else {
            std::cout << "> " << std::flush;
            std::string line;
            if (!std::getline(std::cin, line)) break;   // EOF
            if (line == "quit" || line == "q") break;
            if (auto m = find_move(g.state, line)) {
                g.play(*m);
            } else {
                std::cout << "Illegal or unknown move: '" << line << "'\n";
            }
        }
    }

    std::cout << '\n' << ascii_board(g.state);
    switch (g.result()) {
        case Result::Checkmate:
            std::cout << "Checkmate! " << (g.winner() == Color::White ? "White" : "Black")
                      << " wins.\n";
            break;
        case Result::Stalemate:    std::cout << "Stalemate. Draw.\n"; break;
        case Result::FiftyMoveDraw: std::cout << "Draw (50-move rule).\n"; break;
        case Result::Ongoing:      std::cout << "Game ended.\n"; break;
    }
    return 0;
}

} // namespace chess
