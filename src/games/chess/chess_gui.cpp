// =============================================================================
//  games/chess/chess_gui.cpp
// =============================================================================
#include "games/chess/chess_gui.hpp"

#include "engine/color.hpp"
#include "games/chess/search.hpp"

namespace chess {
namespace {

constexpr int kSquare = 28;   // pixels per board square
constexpr int kOX     = 16;   // board origin x (left)
constexpr int kOY     = 23;   // board origin y (top); board is 224x224

char piece_letter(PieceType t) {
    switch (t) {
        case PieceType::Pawn:   return 'P';
        case PieceType::Knight: return 'N';
        case PieceType::Bishop: return 'B';
        case PieceType::Rook:   return 'R';
        case PieceType::Queen:  return 'Q';
        case PieceType::King:   return 'K';
        default:                return '?';
    }
}

} // namespace

ChessScene::ChessScene(bool vs_ai, int ai_depth)
    : vs_ai_(vs_ai), ai_depth_(ai_depth) {}

int ChessScene::square_at(int mx, int my) const {
    const int fx = (mx - kOX) / kSquare;
    const int fy = (my - kOY) / kSquare;
    if (mx < kOX || my < kOY || fx < 0 || fx > 7 || fy < 0 || fy > 7) return -1;
    const int rank = 7 - fy;  // screen y grows downward; rank 1 is at the bottom
    return make_square(fx, rank);
}

void ChessScene::on_click(int sq) {
    if (sq < 0) return;
    const State& st = game_.state;

    if (selected_ < 0) {
        const Piece p = st.at(sq);
        if (!p.is_none() && p.color == st.side_to_move) selected_ = sq;  // pick up
        return;
    }
    if (sq == selected_) { selected_ = -1; return; }  // click same square: drop

    // Try to move selected -> sq (auto-promote to queen for v1).
    const Move* chosen = nullptr;
    Move pick{};
    for (const Move& m : game_.legal_moves()) {
        if (m.from == selected_ && m.to == sq) {
            if (m.promotion == PieceType::None || m.promotion == PieceType::Queen) {
                pick = m; chosen = &pick; break;       // prefer queen promo / normal
            }
            pick = m; chosen = &pick;                   // fallback
        }
    }
    if (chosen) {
        game_.play(*chosen);
        selected_ = -1;
    } else {
        const Piece p = st.at(sq);
        selected_ = (!p.is_none() && p.color == st.side_to_move) ? sq : -1;  // reselect/drop
    }
}

void ChessScene::update(double dt, const platform::InputState& in) {
    using K  = platform::Key;
    using MB = platform::MouseButton;

    // Space restarts (latched to one action per press).
    if (in.down(K::Space) && !space_latched_) {
        space_latched_ = true;
        game_.reset();
        selected_ = -1;
        ai_timer_ = 0.0;
    }
    if (!in.down(K::Space)) space_latched_ = false;

    if (game_.result() != Result::Ongoing) return;

    const bool white   = (game_.state.side_to_move == Color::White);
    const bool ai_turn = vs_ai_ && !white;  // AI plays Black
    if (ai_turn) {
        ai_timer_ += dt;
        if (ai_timer_ > 0.35) {            // brief pause so the board updates first
            const SearchResult r = search(game_.state, ai_depth_);
            game_.play(r.best);
            selected_ = -1;
            ai_timer_ = 0.0;
        }
        return;
    }

    // Human turn: one click = one action (latched on the button level).
    const bool ldown = in.down(MB::Left);
    if (ldown && !click_latched_) {
        click_latched_ = true;
        on_click(square_at(in.mouse_x, in.mouse_y));
    }
    if (!ldown) click_latched_ = false;
}

void ChessScene::render(const engine::Context& ctx) {
    gfx::Renderer2D& g = ctx.gfx;
    const State& st = game_.state;

    g.clear(gfx::rgb(24, 24, 28));

    // Board squares + pieces.
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const int x = kOX + file * kSquare;
            const int y = kOY + (7 - rank) * kSquare;
            const bool dark = ((file + rank) % 2 == 0);  // a1 (0,0) is dark
            g.fill_rect(x, y, kSquare, kSquare,
                        dark ? gfx::rgb(118, 90, 60) : gfx::rgb(222, 202, 170));

            const Piece p = st.at(make_square(file, rank));
            if (!p.is_none()) {
                const char glyph[2] = { piece_letter(p.type), 0 };
                const gfx::Color col = (p.color == Color::White) ? gfx::rgb(250, 250, 250)
                                                                 : gfx::rgb(18, 18, 18);
                g.draw_text(x + 6, y + 6, glyph, col, 2);  // 16px glyph centered in 28px
            }
        }
    }

    // Selection highlight + legal-move dots.
    if (selected_ >= 0) {
        const int sf = file_of(selected_), sr = rank_of(selected_);
        g.draw_rect(kOX + sf * kSquare, kOY + (7 - sr) * kSquare, kSquare, kSquare,
                    gfx::rgb(240, 230, 80));
        for (const Move& m : game_.legal_moves()) {
            if (m.from == selected_) {
                const int tf = file_of(m.to), tr = rank_of(m.to);
                const int cx = kOX + tf * kSquare + kSquare / 2;
                const int cy = kOY + (7 - tr) * kSquare + kSquare / 2;
                g.fill_rect(cx - 3, cy - 3, 6, 6, gfx::rgba(60, 200, 90, 190));
            }
        }
    }

    // Status panel (right of the board).
    int px = 250, py = 24;
    g.draw_text(px, py, "HAND-ENGINE CHESS", gfx::colors::white, 1); py += 14;
    g.draw_text(px, py, vs_ai_ ? "Human(W) vs AI(B)" : "Human vs Human",
                gfx::rgb(170, 180, 200), 1); py += 18;

    const Result res = game_.result();
    if (res == Result::Ongoing) {
        g.draw_text(px, py, (st.side_to_move == Color::White) ? "White to move" : "Black to move",
                    gfx::colors::white, 1); py += 12;
        if (game_.is_check()) g.draw_text(px, py, "CHECK!", gfx::rgb(240, 120, 120), 1);
        else if (vs_ai_ && st.side_to_move == Color::Black)
            g.draw_text(px, py, "AI thinking...", gfx::rgb(160, 200, 240), 1);
    } else {
        const char* msg = "Game over";
        if      (res == Result::Checkmate)
            msg = (game_.winner() == Color::White) ? "Checkmate! White wins"
                                                   : "Checkmate! Black wins";
        else if (res == Result::Stalemate)    msg = "Stalemate - draw";
        else if (res == Result::FiftyMoveDraw) msg = "Draw (50-move rule)";
        g.draw_text(px, py, msg, gfx::rgb(240, 220, 120), 1);
    }

    g.draw_text(px, 236, "Click piece, click target", gfx::rgb(140, 150, 170), 1);
    g.draw_text(px, 248, "SPACE: new game", gfx::rgb(140, 150, 170), 1);
}

} // namespace chess
