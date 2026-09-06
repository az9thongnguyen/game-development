// =============================================================================
//  games/farm/controls.hpp  —  on-screen controls, so the game is playable by hand
// =============================================================================
//  The farm has been playable in a browser since chapter 118 and unplayable on a
//  phone the whole time: every verb is a key. This is the other half — a d-pad and
//  the action buttons, drawn over the world.
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
//  That rule is why the HOTBAR is in here too (chapter 126). It was laid out with its
//  own constants inside render() while it was only a picture; the moment it had to
//  answer a tap it became a second layout for the same screen, which is precisely the
//  drift this file exists to stop. Its geometry moved here rather than being copied.
//
//  PURE: numbers in, numbers out. No renderer, no input struct, no engine types, and
//  no includes at all, so the geometry is unit-testable without a window. The prices
//  in here (16, 8, 4) are the theme's spacing written out rather than included: this
//  file is the one that decides where these controls go, and a screen laid out from
//  two sources is the bug in the paragraph above.
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
//
// An EMPTY box is a control that is not there this frame, and it needs no guard
// anywhere: `contains` is false for every point in a zero-width box, so a button that
// was not laid out cannot be hit even by code that forgets to ask. That is deliberate.
// The alternative — a `bool has_save` beside the box — is a second fact about the same
// thing, and two facts about one thing eventually disagree.
struct Layout {
    Box up, down, left, right;   // the d-pad, bottom left
    Box use, seed;               // the thumb row, bottom right
    Box save;                    // above `use` — not a game verb, so not on that row
    Box keep, take;              // the cloud conflict's two answers; empty unless asked
    Box tool[4];                 // the hotbar slots: Hoe, Water, Seed, Harvest

    // Whether the PAD is there. The hotbar is not part of this answer: it is drawn on
    // every screen the game has, including the retro framebuffer where the pad is not.
    [[nodiscard]] bool visible() const { return !use.empty(); }
};

// Lay the controls out for a `w` x `h` framebuffer. The pad comes back EMPTY when the
// screen is too small to hold it without covering the world — a control that hides
// what it is acting on is worse than no control.
//
// `conflict` is the one piece of STATE this file takes, and it changes the layout
// rather than being filtered by the caller: while the cloud has a save that disagrees
// with yours, the top-right button stops being `save` and becomes the two answers.
// Suppressing `save` there is not tidiness. Saving during a conflict silently means
// "mine wins" (`save_game` pushes), and a phone player who never saw the F6/F7 line
// would be resolving a question they were never asked.
Layout layout(int w, int h, bool conflict);

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
    int  tool = -1;         // a hotbar slot was tapped: 0..3, like the number keys
    bool save = false;      // ...like F5
    bool keep = false;      // ...like F6
    bool take = false;      // ...like F7
    // The pointer is over a control. The world must then ignore it: without this, a
    // tap on the d-pad also tills the tile underneath, which is the exact bug that
    // makes an on-screen pad feel broken rather than absent.
    bool consumed = false;
};

Action read(const Layout& l, const Pointer& p);

} // namespace farm
