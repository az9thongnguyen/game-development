// =============================================================================
//  engine/ui/touch.hpp  —  the parts of an on-screen control that are not a game
// =============================================================================
//  Chapter 126 gave the farm a d-pad and thumb buttons so a phone could play it,
//  and `CLAUDE.md` recorded the obvious next question and its answer: *if a second
//  game needs these, `farm/controls.hpp` will have to split — but there is no second
//  user, so it has not.* Chapter 137 is the second user.
//
//  What comes out is deliberately SMALL, and the line it is cut along matters. Two
//  things here are facts about a HAND:
//
//    * `kBtn` — a finger is about 9 mm across, so 44 logical pixels is the smallest
//      square that can be hit reliably without looking at it. Below that a d-pad
//      becomes a game about aiming, which is not the game.
//    * `pad_fits` — whether controls fit is a question about PROPORTION, not pixels.
//      The pad and the two action buttons may take at most half the WIDTH, and the
//      pad at most two fifths of the HEIGHT, or a control ends up covering the thing
//      it is acting on, which is worse than no control at all. (The first version of
//      this used fixed minimums and drew a 44 px pad over a 480x270 framebuffer.)
//
//  Those are true of any game with a thumb on it, so they live here.
//
//  WHAT STAYS IN EACH GAME IS THE LAYOUT. The farm's pad sits above a four-slot
//  hotbar; the creature game's sits above nothing and shares the screen with a
//  battle menu. Sharing those rectangles would mean one screen is laid out for the
//  other's neighbours — and the rule both games follow (ONE layout function, read by
//  the renderer AND the hit test) is a discipline, not a shape. A control drawn in
//  one place and hit in another is invisible in a screenshot: the button looks right
//  and does nothing. That bug is prevented by each game having exactly one layout,
//  not by both games having the same one.
//
//  PURE: no includes, no engine types, no input struct. Numbers in, numbers out, so
//  the geometry is unit-testable without a window.
// =============================================================================
#pragma once

namespace touch {

struct Box {
    int x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
    [[nodiscard]] bool empty() const { return w <= 0 || h <= 0; }
    // Do these two overlap? An empty box overlaps nothing, which is the same rule
    // `contains` follows and the reason neither game carries a `bool has_x` beside a
    // box. Here rather than in a test, because "these two controls do not sit on top of
    // each other" is a claim about a HAND — the same kind of fact as the 44 px minimum —
    // and both games' layouts have to be able to assert it.
    [[nodiscard]] bool overlaps(const Box& o) const {
        if (empty() || o.empty()) return false;
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
};

// What the pointer is doing, in FRAMEBUFFER coordinates — the same space the controls
// are laid out in, so no transform stands between drawing and hitting. -1 means the
// pointer is not on screen (the Studio's Play viewport blanks it when the mouse
// leaves), and every hit test is false for it without needing a guard.
struct Pointer {
    int  x = -1, y = -1;
    bool down    = false;   // held this frame
    bool pressed = false;   // went down this frame
};

inline constexpr int kBtn    = 44;   // the size a control must be to be HIT
inline constexpr int kGap    = 6;
inline constexpr int kMargin = 16;
inline constexpr int kPadSpan = kBtn * 3 + kGap * 2;   // three buttons square

// `reserved_bottom` is whatever the game keeps below the pad — the farm's hotbar, or
// just a margin. It is a parameter rather than a constant because it is the one part
// of this that IS about a particular game.
inline bool pad_fits(int w, int h, int reserved_bottom) {
    if (kPadSpan + kBtn * 2 + kGap > w / 2) return false;
    if (kPadSpan > h * 2 / 5) return false;
    if (h < kMargin + kPadSpan + reserved_bottom) return false;
    return true;
}

struct DPad {
    Box up, down, left, right;
    int x = 0, y = 0;      // the pad's own top-left, so a caller can align to it
};

// The pad, bottom left. Returns all-empty boxes when it does not fit, which needs no
// guard at the call site: `contains` is false for every point in a zero-width box.
inline DPad dpad(int w, int h, int reserved_bottom) {
    DPad d;
    if (!pad_fits(w, h, reserved_bottom)) return d;
    d.x = kMargin;
    d.y = h - reserved_bottom - kPadSpan;
    d.up    = Box{d.x + kBtn + kGap,       d.y,                     kBtn, kBtn};
    d.left  = Box{d.x,                     d.y + kBtn + kGap,       kBtn, kBtn};
    d.right = Box{d.x + (kBtn + kGap) * 2, d.y + kBtn + kGap,       kBtn, kBtn};
    d.down  = Box{d.x + kBtn + kGap,       d.y + (kBtn + kGap) * 2, kBtn, kBtn};
    return d;
}

} // namespace touch
