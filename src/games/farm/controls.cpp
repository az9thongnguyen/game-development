// =============================================================================
//  games/farm/controls.cpp  —  see controls.hpp
// =============================================================================
#include "games/farm/controls.hpp"

namespace farm {
namespace {

// A finger is about 9 mm across; at the scale this game renders, 44 logical pixels is
// the smallest square that can be hit reliably without looking at it. Below that a
// d-pad becomes a game about aiming, which is not the game.
constexpr int kBtn = 44;
constexpr int kGap = 6;

// The hotbar strip lives along the bottom (`kSlotH` 24 plus its margin), and the
// controls must clear it: two things that both answer a tap in the same pixels is a
// coin toss the player always loses.
constexpr int kHudBottom = 40;

constexpr int kMargin = 16;

} // namespace

Layout layout(int w, int h) {
    const int pad_w    = kBtn * 3 + kGap * 2;
    const int pad_h    = kBtn * 3 + kGap * 2;
    const int action_w = kBtn * 2 + kGap;

    // Whether the controls fit is a question about PROPORTION, not about pixels. A
    // button has to stay 44 logical pixels to be hittable, so on a small framebuffer
    // the pad stops being an overlay and becomes the screen — and a control that
    // covers what it acts on is worse than one that is absent. Two rules, each with a
    // reason rather than a threshold that happened to look right:
    //
    //   * the controls may take at most half the WIDTH, so at least half is world
    //   * the d-pad may take at most two fifths of the HEIGHT, so the player can see
    //     where they are walking to
    //
    // The first version of this used fixed minimums and drew the pad over the 480x270
    // retro framebuffer, where three 44px buttons are half the screen's height.
    if (pad_w + action_w > w / 2) return Layout{};
    if (pad_h > h * 2 / 5) return Layout{};
    if (h < kMargin + pad_h + kHudBottom) return Layout{};

    Layout l;
    // Bottom left, sitting on the HUD strip.
    const int px = kMargin;
    const int py = h - kHudBottom - (kBtn * 3 + kGap * 2);
    l.up    = Box{px + kBtn + kGap, py, kBtn, kBtn};
    l.left  = Box{px, py + kBtn + kGap, kBtn, kBtn};
    l.right = Box{px + (kBtn + kGap) * 2, py + kBtn + kGap, kBtn, kBtn};
    l.down  = Box{px + kBtn + kGap, py + (kBtn + kGap) * 2, kBtn, kBtn};

    // Bottom right. `use` is the one pressed constantly, so it takes the outer
    // position — the one a thumb reaches without moving the hand.
    const int ay = py + kBtn + kGap;
    l.use  = Box{w - kMargin - kBtn, ay, kBtn, kBtn};
    l.seed = Box{w - kMargin - kBtn * 2 - kGap, ay, kBtn, kBtn};
    return l;
}

Action read(const Layout& l, const Pointer& p) {
    Action a;
    if (!l.visible() || p.x < 0 || p.y < 0) return a;

    // `consumed` is set by POSITION, not by the button being down. A pointer resting
    // over a control still has to stop the world reading it, or the tile under the
    // d-pad highlights and reacts to a click meant for the pad.
    const Box* boxes[] = {&l.up, &l.down, &l.left, &l.right, &l.use, &l.seed};
    for (const Box* b : boxes)
        if (b->contains(p.x, p.y)) { a.consumed = true; break; }
    // No `if (!consumed) return` here. It read as a fast path and was a REDUNDANT
    // guard: every branch below already tests `contains`, so deleting it changed
    // nothing and no test could tell — which is exactly the shape a mutation survives
    // in (chapter 121, and again in 122).

    // Direction is HELD, like an arrow key: the player holds a thumb on `right` and
    // walks. The actions are EDGES, like Z and Q: holding them must not repeat.
    if (p.down) {
        if      (l.left.contains(p.x, p.y))  a.dx = -1;
        else if (l.right.contains(p.x, p.y)) a.dx = 1;
        else if (l.up.contains(p.x, p.y))    a.dy = -1;
        else if (l.down.contains(p.x, p.y))  a.dy = 1;
    }
    if (p.pressed) {
        if (l.use.contains(p.x, p.y))  a.use = true;
        if (l.seed.contains(p.x, p.y)) a.seed = true;
    }
    return a;
}

} // namespace farm
