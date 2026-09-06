// =============================================================================
//  games/creatures/controls.hpp  —  every rectangle on this game's screen
// =============================================================================
//  Same discipline as the farm's (chapter 126), and deliberately NOT the same file.
//  What both games share is in `engine/ui/touch.hpp`: the 44-pixel minimum, the
//  proportion rule, the d-pad's arithmetic — the parts that are facts about a hand.
//  What is here is the LAYOUT, and it is different because the screens are: the
//  farm's pad sits above a four-slot hotbar, and this one sits above nothing while
//  the bottom third of the screen belongs to a battle menu that does not exist there.
//
//  ONE layout function, read by the renderer AND the hit test. A control drawn in one
//  place and hit in another is invisible in a screenshot — the button looks right and
//  does nothing — and this project has shipped that bug four times (chapters 126,
//  127, 132, 135). The only defence that has ever worked is the two readers being the
//  same numbers.
//
//  The battle is a MODE, and the mode changes the layout rather than being filtered
//  by the caller. While the move list is up there is no RUN button anywhere on the
//  screen, so a stale rectangle cannot be hit by a caller that forgot to check — an
//  empty Box's `contains` is false for every point, which is the farm's rule and the
//  reason neither file carries a `bool has_x` beside a box.
//
//  PURE: numbers in, numbers out.
// =============================================================================
#pragma once

#include "engine/ui/touch.hpp"

namespace creature {

using Box     = touch::Box;
using Pointer = touch::Pointer;

// What the screen is asking for right now.
enum class Mode : unsigned char {
    Overworld = 0,   // walking: the pad and two buttons
    Menu,            // in a battle: Fight / Ball / Party / Run
    Moves,           // ...Fight was chosen: four moves, and Back
    Party,           // ...Party was chosen: six slots, and Back
    Ack              // the battle ended: one button to carry on
};

// `cell` is six boxes because the widest mode needs six; a mode that needs four
// leaves the last two EMPTY rather than having its own array. One array means the hit
// test is one loop, and a loop cannot forget the mode it is in.
struct Layout {
    Box up, down, left, right;   // the d-pad — Overworld only
    Box act, save;               // the thumb row — Overworld only
    Box cell[6];                 // Menu / Moves / Party
    Box back;                    // Moves / Party
    Box ack;                     // Ack
    Box panel;                   // the strip the cells sit in; the renderer's
    Box log;                     // where narration goes — not a control, but it must
                                 // not overlap one, so it is decided here too

    [[nodiscard]] bool pad_visible() const { return !act.empty(); }
};

Layout layout(int w, int h, Mode mode);

struct Press {
    int  dx = 0, dy = 0;     // a HELD direction, like the arrow keys
    bool act  = false;       // a fresh press, like Z
    bool save = false;       // ...like F5
    int  cell = -1;          // a menu/move/party slot was tapped: 0..5
    bool back = false;
    bool ack  = false;
    // The pointer is over a control, so the world must ignore it. Without this a tap
    // on the pad also counts as a tap on whatever is under it.
    bool consumed = false;
};

Press read(const Layout& l, Mode mode, const Pointer& p);

} // namespace creature
