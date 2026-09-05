// =============================================================================
//  games/farm/controls.hpp  —  on-screen controls, so the game is playable by hand
// =============================================================================
//  The farm has been playable in a browser since chapter 118 and unplayable on a
//  phone the whole time: every verb is a key. This is the other half — a d-pad and
//  two action buttons drawn over the world.
//
//  It reads the POINTER, not touch events. That is the whole reason it is small:
//  SDL synthesizes a mouse from a finger by default, so one implementation serves a
//  tap, a click and a trackpad, and the platform seam needs no new event type. The
//  price is that only one finger is seen at a time — you cannot hold "walk east" and
//  tap "use" together. For a grid game whose step is one tile per press that is a
//  fair trade, and it is named in the chapter as the ceiling it is.
//
//  ONE layout function, called by both the renderer and the hit test. A control that
//  is drawn in one place and hit in another is the bug this shape exists to prevent,
//  and it is invisible in a screenshot — the button looks right and does nothing.
//
//  PURE: numbers in, numbers out. No renderer, no input struct, no engine types, so
//  the geometry is unit-testable without a window.
// =============================================================================
#pragma once

namespace farm {

struct Box {
    int x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] bool contains(int px, int py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
    [[nodiscard]] bool empty() const { return w <= 0 || h <= 0; }
};

// Where every on-screen control sits, in FRAMEBUFFER coordinates — the same space
// the pointer arrives in, so no transform stands between drawing and hitting.
struct Layout {
    Box up, down, left, right;   // the d-pad, bottom left
    Box use, seed;               // the actions, bottom right
    [[nodiscard]] bool visible() const { return !use.empty(); }
};

// Lay the controls out for a `w` x `h` framebuffer. Returns an EMPTY layout when the
// screen is too small to hold them without covering the world — a control that hides
// what it is acting on is worse than no control.
Layout layout(int w, int h);

// What the pointer is doing, in framebuffer coordinates. -1 = the pointer is not on
// screen (the Play viewport blanks it when the mouse leaves).
struct Pointer {
    int  x = -1, y = -1;
    bool down = false;      // held this frame
    bool pressed = false;   // went down this frame
};

struct Action {
    int  dx = 0, dy = 0;    // a HELD direction, like the arrow keys
    bool use  = false;      // a fresh press, like Z
    bool seed = false;      // ...like Q
    // The pointer is over a control. The world must then ignore it: without this, a
    // tap on the d-pad also tills the tile underneath, which is the exact bug that
    // makes an on-screen pad feel broken rather than absent.
    bool consumed = false;
};

Action read(const Layout& l, const Pointer& p);

} // namespace farm
