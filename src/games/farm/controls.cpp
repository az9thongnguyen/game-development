// =============================================================================
//  games/farm/controls.cpp  —  see controls.hpp
// =============================================================================
#include "games/farm/controls.hpp"

namespace farm {
namespace {

// `kBtn` (44), `kGap`, `kMargin` and `kPadSpan` are facts about a HAND, so they live
// in engine/ui/touch.hpp with the proportion rule and the pad arithmetic. Aliased
// rather than re-declared: two numbers that must agree are one number.
using touch::kBtn;
using touch::kGap;
using touch::kMargin;
using touch::kPadSpan;

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
constexpr int kHudTopH = 34;
constexpr int kHintMinW = 160;

// The pad must clear the hotbar, and the hotbar is tall exactly when the pad is
// shown — so the gate depends on the height and the height depends on the gate. The
// circle is broken by measuring against the TALL hotbar always: the reserved strip is
// then a constant, and the error is in the conservative direction (the pad is hidden
// on a screen where the short hotbar would have left it room, never drawn on one
// where the tall hotbar will cover it). Both current sizes clear it by a wide margin;
// the binding rule is the two-fifths one below.
constexpr int kHudBottom = kBtn + kSlotPad * 2;

// The proportion rule itself is `touch::pad_fits`; what this game supplies is what it
// keeps below the pad.
bool pad_fits(int w, int h) { return touch::pad_fits(w, h, kHudBottom); }

// How tall the hotbar strip is on this screen. Asked by the panel too, so the one
// place that decides it is the one place that has to change.
int hud_height(int w, int h) { return pad_fits(w, h) ? kBtn : kSlotH; }

} // namespace

Layout layout(int w, int h, bool conflict) {
    Layout l;
    if (w > 0 && h >= kHudTopH) l.hud = Box{0, 0, w, kHudTopH};

    // ---- the hotbar, on every screen -------------------------------------------
    const bool big    = pad_fits(w, h);
    const int  slot_h = hud_height(w, h);
    const int  strip  = kSlotPad * 2 + kSlotW * 4 + kSlotGap * 3;
    if (w >= strip && h >= slot_h + kSlotPad * 2) {
        const int y = h - slot_h - kSlotPad;
        for (int i = 0; i < 4; ++i)
            l.tool[i] = Box{kSlotPad + i * (kSlotW + kSlotGap), y, kSlotW, slot_h};
        const int hx = l.tool[3].x + l.tool[3].w + 12;
        if (w - hx >= kHintMinW) l.hint = Box{hx, y, w - hx, slot_h};
    }
    // Below that the hotbar is left empty and the renderer draws no hotbar, for the
    // same reason the pad disappears: four slots running off the edge of the screen
    // are not a smaller hotbar, they are a broken one.

    if (!big) return l;

    // ---- the d-pad, bottom left --------------------------------------------------
    const touch::DPad pad = touch::dpad(w, h, kHudBottom);
    const int py = pad.y;
    l.up = pad.up; l.left = pad.left; l.right = pad.right; l.down = pad.down;

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

Action read(const Layout& l, const Pointer* pointers, std::size_t count) {
    Action out;
    if (!pointers) return out;
    int dx = 0, dy = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Action one = read(l, pointers[i]);
        dx += one.dx; dy += one.dy;
        out.use  = out.use  || one.use;
        out.seed = out.seed || one.seed;
        out.save = out.save || one.save;
        out.keep = out.keep || one.keep;
        out.take = out.take || one.take;
        out.consumed = out.consumed || one.consumed;
        if (out.tool < 0 && one.tool >= 0) out.tool = one.tool;
    }
    out.dx = dx < 0 ? -1 : (dx > 0 ? 1 : 0);
    out.dy = dy < 0 ? -1 : (dy > 0 ? 1 : 0);
    return out;
}

// -----------------------------------------------------------------------------
//  The dialogue panel
// -----------------------------------------------------------------------------

Talk talk_layout(int w, int h, int choices) {
    Talk t;
    t.count = choices > 0 ? choices : 0;

    // A row is 44 when it has to be HIT and 18 when it only has to be READ — the same
    // question the hotbar asks, answered from the same fact, so a screen never ends up
    // with a thumb-sized hotbar above a hair-thin option list.
    const int row   = pad_fits(w, h) ? kBtn : 18;
    const int head  = 60;                     // speaker + the line, above the options
    const int box_h = head + kSlotPad + t.count * row;
    // Above the hotbar strip, not a fixed distance from the bottom edge. The panel used
    // to sit at `h - box_h - 12`, which cleared a 24 px hotbar and covers a 44 px one —
    // and the tool and seed count are exactly what a player checks while an NPC is
    // telling them what grows here.
    const int by    = h - hud_height(w, h) - kSlotPad * 2 - box_h;
    // No room for the panel is not an error and not a smaller panel: the box is the
    // one thing on this screen that must be readable, so it either fits or the
    // keyboard is the honest answer. Same rule as the pad, one screen up.
    if (by < kMargin || w < kMargin * 6) return Talk{};

    t.panel = Box{kMargin, by, w - kMargin * 2, box_h};
    t.row   = row;
    t.first = Box{t.panel.x + kSlotPad, by + head, t.panel.w - kSlotPad * 2, row};
    return t;
}

TalkAction read(const Talk& t, const Pointer& p) {
    TalkAction a;
    if (!t.visible() || p.x < 0 || p.y < 0 || !p.pressed) return a;
    // An option wins over the panel it sits in. Every option is INSIDE the panel, so
    // testing the panel first would swallow every answer and turn the whole box into
    // one "next" button — which is what a first draft of this did.
    for (int i = 0; i < t.count; ++i)
        if (t.choice(i).contains(p.x, p.y)) { a.choice = i; return a; }
    if (t.panel.contains(p.x, p.y)) a.advance = true;
    return a;
}

} // namespace farm
