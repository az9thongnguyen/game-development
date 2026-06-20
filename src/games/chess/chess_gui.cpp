// =============================================================================
//  games/chess/chess_gui.cpp
// =============================================================================
#include "games/chess/chess_gui.hpp"

#include <string>

#include "engine/color.hpp"
#include "engine/image.hpp"
#include "games/chess/search.hpp"

namespace chess {
namespace {

constexpr int kSquare  = 80;             // pixels per board square
constexpr int kOX      = 24;             // board origin x (room for rank labels)
constexpr int kOY      = 24;             // board origin y
constexpr int kBoard   = kSquare * 8;    // 640
constexpr int kStatusX = kOX + kBoard + 24;  // 688

char piece_letter(PieceType t) {
    switch (t) {
        case PieceType::Pawn: return 'P'; case PieceType::Knight: return 'N';
        case PieceType::Bishop: return 'B'; case PieceType::Rook: return 'R';
        case PieceType::Queen: return 'Q'; case PieceType::King: return 'K';
        default: return '?';
    }
}

inline int sq_x(int file) { return kOX + file * kSquare; }
inline int sq_y(int rank) { return kOY + (7 - rank) * kSquare; }  // rank 8 at top

} // namespace

ChessScene::ChessScene(bool vs_ai, int ai_depth)
    : vs_ai_(vs_ai), ai_depth_(ai_depth) {
    struct Entry { PieceType t; char c; };
    const Entry map[6] = {
        {PieceType::Pawn, 'P'}, {PieceType::Knight, 'N'}, {PieceType::Bishop, 'B'},
        {PieceType::Rook, 'R'}, {PieceType::Queen, 'Q'},  {PieceType::King, 'K'},
    };
    for (int ci = 0; ci < 2; ++ci) {
        const char cc = (ci == 0) ? 'w' : 'b';
        for (const Entry& e : map) {
            if (auto img = gfx::load_image(std::string("pieces/") + cc + e.c + ".hrt"))
                images_[ci][static_cast<int>(e.t)] = *img;
            else
                images_ok_ = false;
        }
    }
}

int ChessScene::square_at(int mx, int my) const {
    const int fx = (mx - kOX) / kSquare;
    const int fy = (my - kOY) / kSquare;
    if (mx < kOX || my < kOY || fx < 0 || fx > 7 || fy < 0 || fy > 7) return -1;
    return make_square(fx, 7 - fy);
}

void ChessScene::on_click(int sq) {
    if (sq < 0) return;
    const State& st = game_.state;

    if (selected_ < 0) {
        const Piece p = st.at(sq);
        if (!p.is_none() && p.color == st.side_to_move) selected_ = sq;
        return;
    }
    if (sq == selected_) { selected_ = -1; return; }

    const Move* chosen = nullptr;
    Move pick{};
    for (const Move& m : game_.legal_moves()) {
        if (m.from == selected_ && m.to == sq) {
            if (m.promotion == PieceType::None || m.promotion == PieceType::Queen) {
                pick = m; chosen = &pick; break;
            }
            pick = m; chosen = &pick;
        }
    }
    if (chosen) {
        last_from_ = chosen->from;
        last_to_   = chosen->to;
        game_.play(*chosen);
        selected_ = -1;
    } else {
        const Piece p = st.at(sq);
        selected_ = (!p.is_none() && p.color == st.side_to_move) ? sq : -1;
    }
}

void ChessScene::update(double dt, const platform::InputState& in) {
    using K  = platform::Key;
    using MB = platform::MouseButton;

    if (in.down(K::Space) && !space_latched_) {
        space_latched_ = true;
        game_.reset();
        selected_ = -1;
        last_from_ = last_to_ = -1;
        ai_timer_ = 0.0;
    }
    if (!in.down(K::Space)) space_latched_ = false;

    if (game_.result() != Result::Ongoing) return;

    const bool white   = (game_.state.side_to_move == Color::White);
    const bool ai_turn = vs_ai_ && !white;
    if (ai_turn) {
        ai_timer_ += dt;
        if (ai_timer_ > 0.35) {
            const SearchResult r = search(game_.state, ai_depth_);
            last_from_ = r.best.from;
            last_to_   = r.best.to;
            game_.play(r.best);
            selected_ = -1;
            ai_timer_ = 0.0;
        }
        return;
    }

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

    g.clear(gfx::rgb(38, 36, 44));

    // Board frame.
    g.fill_rect(kOX - 6, kOY - 6, kBoard + 12, kBoard + 12, gfx::rgb(60, 46, 34));
    g.draw_rect(kOX - 6, kOY - 6, kBoard + 12, kBoard + 12, gfx::rgb(18, 14, 10));

    // Squares + last-move + selection tints + pieces.
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const int x = sq_x(file), y = sq_y(rank);
            const int sq = make_square(file, rank);
            const bool dark = ((file + rank) % 2 == 0);
            g.fill_rect(x, y, kSquare, kSquare,
                        dark ? gfx::rgb(181, 136, 99) : gfx::rgb(240, 217, 181));

            if (sq == last_from_ || sq == last_to_)
                g.fill_rect(x, y, kSquare, kSquare, gfx::rgba(255, 235, 90, 70));
            if (sq == selected_)
                g.fill_rect(x, y, kSquare, kSquare, gfx::rgba(90, 200, 110, 95));

            const Piece p = st.at(sq);
            if (!p.is_none()) {
                const int ci = (p.color == Color::White) ? 0 : 1;
                const gfx::Image& im = images_[ci][static_cast<int>(p.type)];
                if (!im.pixels.empty()) {
                    g.blit(gfx::Sprite{ im.pixels.data(), im.w, im.h }, x, y);
                } else {
                    const char gl[2] = { piece_letter(p.type), 0 };
                    g.draw_text(x + 16, y + 16, gl,
                                (p.color == Color::White) ? gfx::colors::white
                                                          : gfx::rgb(18, 18, 18), 6);
                }
            }
        }
    }

    // Legal-move markers for the selected piece.
    if (selected_ >= 0) {
        for (const Move& m : game_.legal_moves()) {
            if (m.from == selected_) {
                const int cx = sq_x(file_of(m.to)) + kSquare / 2;
                const int cy = sq_y(rank_of(m.to)) + kSquare / 2;
                g.fill_rect(cx - 7, cy - 7, 14, 14, gfx::rgba(40, 170, 70, 200));
            }
        }
    }

    // Coordinate labels.
    for (int file = 0; file < 8; ++file) {
        const char s[2] = { static_cast<char>('a' + file), 0 };
        g.draw_text(sq_x(file) + kSquare / 2 - 4, kOY + kBoard + 10, s, gfx::rgb(170, 170, 184), 1);
    }
    for (int rank = 0; rank < 8; ++rank) {
        const char s[2] = { static_cast<char>('1' + rank), 0 };
        g.draw_text(8, sq_y(rank) + kSquare / 2 - 4, s, gfx::rgb(170, 170, 184), 1);
    }

    // Status panel.
    int px = kStatusX, py = 30;
    g.draw_text(px, py, "HAND-ENGINE", gfx::colors::white, 2); py += 22;
    g.draw_text(px, py, "CHESS",       gfx::colors::white, 2); py += 34;
    g.draw_text(px, py, vs_ai_ ? "You (W) vs AI (B)" : "Human vs Human",
                gfx::rgb(170, 180, 200), 1); py += 30;

    const Result res = game_.result();
    if (res == Result::Ongoing) {
        g.draw_text(px, py, (st.side_to_move == Color::White) ? "White to move" : "Black to move",
                    gfx::colors::white, 2); py += 26;
        if (game_.is_check())
            g.draw_text(px, py, "CHECK!", gfx::rgb(245, 120, 120), 2);
        else if (vs_ai_ && st.side_to_move == Color::Black)
            g.draw_text(px, py, "AI thinking...", gfx::rgb(150, 200, 245), 1);
    } else {
        const char* msg = "Game over";
        if      (res == Result::Checkmate)
            msg = (game_.winner() == Color::White) ? "Checkmate! White" : "Checkmate! Black";
        else if (res == Result::Stalemate)     msg = "Stalemate - draw";
        else if (res == Result::FiftyMoveDraw) msg = "Draw (50-move)";
        g.draw_text(px, py, msg, gfx::rgb(245, 222, 120), 2);
    }

    g.draw_text(px, kOY + kBoard - 28, "Click a piece,",   gfx::rgb(150, 158, 175), 1);
    g.draw_text(px, kOY + kBoard - 16, "then a green dot.", gfx::rgb(150, 158, 175), 1);
    g.draw_text(px, kOY + kBoard,      "SPACE: new game",   gfx::rgb(150, 158, 175), 1);
}

} // namespace chess
