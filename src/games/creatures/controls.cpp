// =============================================================================
//  games/creatures/controls.cpp  —  see controls.hpp
// =============================================================================
#include "games/creatures/controls.hpp"

#include <algorithm>

namespace creature {
namespace {

using touch::kBtn;
using touch::kGap;
using touch::kMargin;

// The battle panel: two rows of two (or three of two for a party), across the bottom
// half of the width, with the narration beside it. 96 is two 44-px rows plus the gap
// — the panel is sized by what has to be HITTABLE inside it, not by what looks right.
constexpr int kRowH  = kBtn;
constexpr int kPanelH = kRowH * 2 + kGap * 3;
constexpr int kPad    = 8;

bool in_battle(Mode m) { return m != Mode::Overworld; }

} // namespace

const char* label(Control control) {
    switch (control) {
        case Control::Up:     return "^";
        case Control::Down:   return "v";
        case Control::Left:   return "<";
        case Control::Right:  return ">";
        case Control::Act:    return "ACT";
        case Control::Save:   return "SAVE";
        case Control::Online: return "PVP";
    }
    return "";
}

Layout layout(int w, int h, Mode mode) {
    Layout l;
    if (w <= 0 || h <= 0) return l;

    if (!in_battle(mode)) {
        // A product HUD, not a debug sentence. The health and progress regions are
        // children of this card; the message uses the safe lane between both thumbs.
        if (w >= 360 && h >= 120) {
            l.hud     = Box{kPad, kPad, w - kPad * 2, 44};
            l.health  = Box{kPad * 2, 36, 140, 8};
            l.stats   = Box{176, 16, w - 192, 28};
        }
        if (w >= 400 && h >= 180)
            l.message = Box{168, h - 40, w - 236, 32};

        // ---- walking ------------------------------------------------------------
        const touch::DPad pad = touch::dpad(w, h, kMargin);
        l.up = pad.up; l.left = pad.left; l.right = pad.right; l.down = pad.down;
        if (pad.up.empty()) return l;      // too small for controls: keys only

        // The thumb row, mirroring the pad's middle row so both hands rest level.
        const int ay = pad.y + kBtn + kGap;
        l.act  = Box{w - kMargin - kBtn, ay, kBtn, kBtn};
        // `save` sits a row ABOVE, off the row a thumb rests on: it is not a game
        // verb, and a reach for `act` must not land on it. The farm learned that one.
        l.save = Box{w - kMargin - kBtn, pad.y, kBtn, kBtn};
        // ...and `online` a row above THAT, for the same reason twice over: starting a
        // rated match by mis-reaching for `act` would be the most annoying stray tap in
        // the game.
        //
        // This had an `if (oy >= kMargin)` guard for about an hour. It was DEAD: the
        // d-pad only appears at all from ~360 px of height, and at that height this row
        // is already 150 px down. A mutation that removed it survived, which is what
        // dead code looks like from the outside. The check that would actually catch a
        // regression lives in the test, sweeping heights and asserting the margin — a
        // guard that silently places no button hides the bug the test would shout about.
        l.online = Box{w - kMargin - kBtn, pad.y - kBtn - kGap, kBtn, kBtn};
        return l;
    }

    // ---- a battle ----------------------------------------------------------------
    // The panel is the bottom strip on every battle mode, so the sprites above it
    // never have to move when the menu changes what it is offering.
    l.panel = Box{0, h - kPanelH, w, kPanelH};
    if (kPanelH + kBtn > h || w < kBtn * 4) return l;   // no room: keys only

    const int right = w / 2;
    l.log = Box{kPad, l.panel.y + kPad, right - kPad * 2, kPanelH - kPad * 2};

    // The two creatures: theirs up and to the right, yours down and to the left, at
    // 4x — the size a 16 px sprite reads at on a 640-wide framebuffer.
    const int sz = 64;
    l.theirs = Box{w - sz - 48, 40, sz, sz};
    l.mine   = Box{48, l.panel.y - sz - 12, sz, sz};
    const int info_w = std::min(220, w / 2 - 48);
    if (h >= 280) {
        l.theirs_info = Box{24, 16, info_w, 52};
        l.mine_info   = Box{w / 2 + 24, l.panel.y - 68, info_w, 52};
    }

    if (mode == Mode::Online) {
        // One control, and it is Cancel — in the same place `back` sits in the other
        // menus, because it is the same verb: get me out of here.
        l.back = Box{kPad, l.panel.y + kPanelH - kBtn - kPad, kBtn * 2, kBtn};
        l.log  = Box{kPad, l.panel.y + kPad, w - kPad * 2, l.back.y - l.panel.y - kPad * 2};
        // No sprites: there is nothing to draw yet. Empty boxes rather than a flag, so
        // a renderer that forgets to check draws nothing instead of drawing at 0,0.
        l.mine = l.theirs = Box{};
        l.mine_info = l.theirs_info = Box{};
        return l;
    }

    if (mode == Mode::Ack) {
        l.ack = Box{w - kMargin - kBtn * 3, l.panel.y + (kPanelH - kBtn) / 2, kBtn * 3, kBtn};
        return l;
    }

    // Two columns, and as many rows as the mode needs. A party is three rows, so its
    // rows are shorter — the panel height is fixed because the sprites above it are.
    const int rows  = mode == Mode::Party ? 3 : 2;
    const int count = mode == Mode::Party ? 6 : 4;
    const int cw = (w - right - kPad * 3) / 2;
    const int ch = (kPanelH - kPad * (rows + 1)) / rows;
    for (int i = 0; i < count; ++i) {
        const int cx = right + kPad + (i % 2) * (cw + kPad);
        const int cy = l.panel.y + kPad + (i / 2) * (ch + kPad);
        l.cell[i] = Box{cx, cy, cw, ch};
    }

    if (mode == Mode::Moves || mode == Mode::Party) {
        // Back sits in the LOG column, inside the panel — the opposite half of the
        // screen from the cells, so a mis-tap cannot cancel the turn you meant to
        // take (the worst button in a turn-based game). It was above the panel until
        // a rendered frame showed it sitting exactly on top of the player's creature:
        // a control drawn over the thing the screen is about.
        l.back = Box{kPad, l.panel.y + kPanelH - kBtn - kPad, kBtn * 2, kBtn};
        l.log.h = std::max(0, l.back.y - l.log.y - kPad);
    }
    return l;
}

Press read(const Layout& l, Mode mode, const Pointer& p) {
    Press a;
    if (p.x < 0 || p.y < 0) return a;

    if (mode == Mode::Overworld) {
        if (l.up.contains(p.x, p.y))    { a.dy = -1; a.consumed = true; }
        if (l.down.contains(p.x, p.y))  { a.dy =  1; a.consumed = true; }
        if (l.left.contains(p.x, p.y))  { a.dx = -1; a.consumed = true; }
        if (l.right.contains(p.x, p.y)) { a.dx =  1; a.consumed = true; }
        // A direction is HELD; the rest are edges, or one tap would fire every frame
        // the finger stayed down.
        if (l.act.contains(p.x, p.y))  { a.act  = p.pressed; a.consumed = true; }
        if (l.save.contains(p.x, p.y)) { a.save = p.pressed; a.consumed = true; }
        if (l.online.contains(p.x, p.y)) { a.online = p.pressed; a.consumed = true; }
        if (!a.consumed) a.dx = a.dy = 0;
        return a;
    }

    if (l.panel.contains(p.x, p.y)) a.consumed = true;
    if (l.back.contains(p.x, p.y))  a.consumed = true;
    if (l.ack.contains(p.x, p.y))   a.consumed = true;
    if (!p.pressed) return a;

    if (l.ack.contains(p.x, p.y))  { a.ack = true;  return a; }
    if (l.back.contains(p.x, p.y)) { a.back = true; return a; }
    for (int i = 0; i < 6; ++i)
        if (l.cell[i].contains(p.x, p.y)) { a.cell = i; return a; }
    return a;
}

Press read(const Layout& l, Mode mode, const Pointer* pointers, std::size_t count) {
    Press out;
    if (!pointers) return out;
    int dx = 0, dy = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Press one = read(l, mode, pointers[i]);
        dx += one.dx; dy += one.dy;
        out.act = out.act || one.act;
        out.save = out.save || one.save;
        out.online = out.online || one.online;
        out.back = out.back || one.back;
        out.ack = out.ack || one.ack;
        out.consumed = out.consumed || one.consumed;
        if (out.cell < 0 && one.cell >= 0) out.cell = one.cell;
    }
    out.dx = dx < 0 ? -1 : (dx > 0 ? 1 : 0);
    out.dy = dy < 0 ? -1 : (dy > 0 ? 1 : 0);
    return out;
}

} // namespace creature
