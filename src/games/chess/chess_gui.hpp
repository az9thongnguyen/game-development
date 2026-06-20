// =============================================================================
//  games/chess/chess_gui.hpp  —  windowed (GUI) frontend for chess
// =============================================================================
//  An engine::Scene that draws the board + pieces with our 2D software renderer,
//  takes mouse input to select and move, highlights legal moves, shows status,
//  and lets the AI reply. It drives the SAME Game controller as the TUI — this is
//  the only chess file that depends on the engine.
// =============================================================================
#pragma once

#include "engine/image.hpp"
#include "engine/scene.hpp"
#include "games/chess/game.hpp"

namespace chess {

class ChessScene : public engine::Scene {
public:
    ChessScene(bool vs_ai, int ai_depth);

    void update(double dt, const platform::InputState& input) override;
    void render(const engine::Context& ctx) override;

private:
    int  square_at(int mouse_x, int mouse_y) const;  // pixel -> square, or -1
    void on_click(int sq);

    Game       game_;
    bool       vs_ai_;
    int        ai_depth_;
    int        selected_ = -1;          // currently selected square, or -1
    bool       click_latched_ = false;  // one action per mouse press
    bool       space_latched_ = false;  // one restart per Space press
    double     ai_timer_ = 0.0;         // small delay so the human's move shows first
    int        last_from_ = -1;         // last move (for highlight)
    int        last_to_ = -1;
    gfx::Image images_[2][7];           // [color 0=W/1=B][PieceType]; real artwork
    bool       images_ok_ = true;       // false -> fall back to letter glyphs
};

} // namespace chess
