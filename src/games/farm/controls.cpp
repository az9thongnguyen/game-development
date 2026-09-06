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

constexpr int kMargin = 16;

// The hotbar. It is drawn on every screen the game has, so it has two heights: the
// 24 px strip it has always been, and a full 44 when there is room for a thumb.
//
// The two are not a preference. `kBtn` is the size a control must be to be HIT; 24 is
// the size a label must be to be READ. While the hotbar was only a label, 24 was the
// right answer and being unhittable was not a flaw. It has to grow exactly when it
// starts answering a tap, which is exactly when the pad fits — one fact, asked once.
constexpr int kSlotW   = 62;
constexpr int kSlotH   = 24;
constexpr int kSlotPad = 8;
constexpr int kSlotGap = 4;

// The pad must clear the hotbar, and the hotbar is tall exactly when the pad is
// shown — so the gate depends on the height and the height depends on the gate. The
// circle is broken by measuring against the TALL hotbar always: the reserved strip is
// then a constant, and the error is in the conservative direction (the pad is hidden
// on a screen where the short hotbar would have left it room, never drawn on one
// where the tall hotbar will cover it). Both current sizes clear it by a wide margin;
// the binding rule is the two-fifths one below.
constexpr int kHudBottom = kBtn + kSlotPad * 2;

constexpr int kPadSpan = kBtn * 3 + kGap * 2;   // the d-pad is three buttons square

bool pad_fits(int w, int h) {
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
    if (kPadSpan + kBtn * 2 + kGap > w / 2) return false;
    if (kPadSpan > h * 2 / 5) return false;
    if (h < kMargin + kPadSpan + kHudBottom) return false;
    return true;
}

// How tall the hotbar strip is on this screen — 44 when it has to be hit, 24 when it
// only has to be read. One function, because it is one fact.
int hud_height(int w, int h) { return pad_fits(w, h) ? kBtn : kSlotH; }

} // namespace

Layout layout(int w, int h, bool conflict) {
    Layout l;

    // ---- the hotbar, on every screen -------------------------------------------
    const bool big    = pad_fits(w, h);
    const int  slot_h = hud_height(w, h);
    const int  strip  = kSlotPad * 2 + kSlotW * 4 + kSlotGap * 3;
    if (w >= strip && h >= slot_h + kSlotPad * 2) {
        const int y = h - slot_h - kSlotPad;
        for (int i = 0; i < 4; ++i)
            l.tool[i] = Box{kSlotPad + i * (kSlotW + kSlotGap), y, kSlotW, slot_h};
    }
    // Below that the hotbar is left empty and the renderer draws no hotbar, for the
    // same reason the pad disappears: four slots running off the edge of the screen
    // are not a smaller hotbar, they are a broken one.

    if (!big) return l;

    // ---- the d-pad, bottom left --------------------------------------------------
    const int px = kMargin;
    const int py = h - kHudBottom - kPadSpan;
    l.up    = Box{px + kBtn + kGap, py, kBtn, kBtn};
    l.left  = Box{px, py + kBtn + kGap, kBtn, kBtn};
    l.right = Box{px + (kBtn + kGap) * 2, py + kBtn + kGap, kBtn, kBtn};
    l.down  = Box{px + kBtn + kGap, py + (kBtn + kGap) * 2, kBtn, kBtn};

    // ---- the actions, bottom right -----------------------------------------------
    // The middle row is where a thumb rests, so the two verbs pressed constantly live
    // there and `use` — the one pressed most — takes the outer seat, the one a thumb
    // reaches without moving the hand.
    const int outer = w - kMargin - kBtn;
    const int inner = w - kMargin - kBtn * 2 - kGap;
    const int ay    = py + kBtn + kGap;
    l.use  = Box{outer, ay, kBtn, kBtn};
    l.seed = Box{inner, ay, kBtn, kBtn};

    // The row above is for what is NOT a game verb, so a reach for `use` cannot land
    // on it. Normally that is `save`; during a cloud conflict it is the conflict's two
    // answers instead, and `keep` takes the easy outer seat because it is the one that
    // changes nothing. `take` throws away the play on this device, so it costs a
    // deliberate stretch — a destructive answer should never be the comfortable one.
    if (conflict) {
        l.keep = Box{outer, py, kBtn, kBtn};
        l.take = Box{inner, py, kBtn, kBtn};
    } else {
        l.save = Box{outer, py, kBtn, kBtn};
    }
    return l;
}

Action read(const Layout& l, const Pointer& p) {
    Action a;
    if (p.x < 0 || p.y < 0) return a;
    // No `l.visible()` test here. The hotbar outlives the pad — it is laid out on the
    // retro framebuffer where the pad is not — so a short-circuit on the pad would
    // make the tool slots dead on exactly the screens that still have them.

    // `consumed` is set by POSITION, not by the button being down. A pointer resting
    // over a control still has to stop the world reading it, or the tile under the
    // d-pad highlights and reacts to a click meant for the pad.
    const Box* boxes[] = {&l.up,   &l.down, &l.left, &l.right,
                          &l.use,  &l.seed, &l.save, &l.keep, &l.take,
                          &l.tool[0], &l.tool[1], &l.tool[2], &l.tool[3]};
    for (const Box* b : boxes)
        if (b->contains(p.x, p.y)) { a.consumed = true; break; }
    // No `if (!consumed) return` here. It read as a fast path and was a REDUNDANT
    // guard: every branch below already tests `contains`, so deleting it changed
    // nothing and no test could tell — which is exactly the shape a mutation survives
    // in (chapter 121, and again in 122).

    // Direction is HELD, like an arrow key: the player holds a thumb on `right` and
    // walks. Everything else is an EDGE, like Z and Q: holding must not repeat.
    if (p.down) {
        if      (l.left.contains(p.x, p.y))  a.dx = -1;
        else if (l.right.contains(p.x, p.y)) a.dx = 1;
        else if (l.up.contains(p.x, p.y))    a.dy = -1;
        else if (l.down.contains(p.x, p.y))  a.dy = 1;
    }
    if (p.pressed) {
        if (l.use.contains(p.x, p.y))  a.use  = true;
        if (l.seed.contains(p.x, p.y)) a.seed = true;
        if (l.save.contains(p.x, p.y)) a.save = true;
        if (l.keep.contains(p.x, p.y)) a.keep = true;
        if (l.take.contains(p.x, p.y)) a.take = true;
        for (int i = 0; i < 4; ++i)
            if (l.tool[i].contains(p.x, p.y)) a.tool = i;
    }
    return a;
}

} // namespace farm
