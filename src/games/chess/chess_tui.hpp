// =============================================================================
//  games/chess/chess_tui.hpp  —  terminal (text) frontend for chess
// =============================================================================
//  A pure stdin/stdout frontend: no engine window, no SDL. It drives the same
//  Game controller as the GUI, proving the chess core is truly UI-agnostic.
//  Human is White; with vs_ai, the AI plays Black at the given search depth.
// =============================================================================
#pragma once

namespace chess {

int run_tui(bool vs_ai, int ai_depth);

} // namespace chess
