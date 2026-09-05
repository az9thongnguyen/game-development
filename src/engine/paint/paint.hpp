// =============================================================================
//  engine/paint/paint.hpp  —  pixel edits, expressed as undoable commands
// =============================================================================
//  `command_stack.hpp` has said since chapter 113 that "a map workspace, a scene
//  workspace and a pixel editor all share one history implementation". This is the
//  third one arriving, and it is deliberately the SAME shape as `mapedit::`: an edit
//  operation returns a `doc::Command` rather than mutating and hoping somebody
//  remembers to record it.
//
//  Same shape, because the two problems are the same problem. A stroke accumulates
//  here rather than merging on the stack, for exactly the reason map_edit gives: the
//  stack's merge keeps the FIRST revert and the LATEST apply, which is right for a
//  gesture whose latest state subsumes every earlier one and WRONG for one that
//  accumulates — undoing a painted line would restore only its first pixel.
//
//  Why this exists at all, when the Texture Lab already makes `.hrt` files: the Lab
//  GENERATES. Every pixel it can produce is a function of twelve numbers, which is
//  why it can draw water and cannot draw a bucket. Anything with a SHAPE needs a
//  human moving a cursor, and chapter 122 found that out the honest way — the farm's
//  path is one tile wide, no pack in hand has a narrow-strip piece for it, and no
//  amount of noise will make one.
//
//  PURE: no renderer, no I/O, no UI. The `gfx::Image&` a command captures must
//  outlive the stack it is pushed onto — in practice both live in the workspace,
//  which is neither copied nor moved.
// =============================================================================
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "engine/document/command_stack.hpp"
#include "engine/image.hpp"

namespace paint {

// One pixel's before/after. Storing `before` per pixel is what makes undo exact for
// a stroke that crossed several different colours.
struct PixelEdit {
    int        x = 0, y = 0;
    gfx::Color before = 0, after = 0;
};

// Pixels a filled rectangle covers, clamped to the image, corners in any order.
std::vector<PixelEdit> rect_pixels(const gfx::Image& img, int x0, int y0, int x1, int y1,
                                   gfx::Color c);

// The 4-connected region of pixels matching the colour under (x,y), repainted to `c`.
// Empty when the region already holds `c` (every pixel would be a no-op).
//
// Matching is on the WHOLE colour including alpha, not on RGB: two pixels that differ
// only in alpha are different pixels, and a fill that ignored that would silently make
// a transparent area opaque.
std::vector<PixelEdit> flood_pixels(const gfx::Image& img, int x, int y, gfx::Color c);

// Drop pixels that change nothing. An "edit" whose before == after would still become
// an undo step, and Ctrl+Z would then appear not to work.
void drop_noops(std::vector<PixelEdit>& px);

// A command that writes `px` into `img`. Returns nullopt when there is nothing to do,
// so the caller cannot accidentally push an empty step.
std::optional<doc::Command> make_command(gfx::Image& img, std::vector<PixelEdit> px,
                                         std::string label);

// A drag. Pixels are written through as the mouse moves — you have to see the paint
// under the cursor — and the whole gesture reaches the stack as ONE command when the
// button comes up. Re-applying it is therefore idempotent, which is what redo needs.
class Stroke {
public:
    // The image is bound once, at the start of the gesture, rather than passed to
    // every touch: the command finish() returns has to write to the same image the
    // stroke painted, and a per-call parameter is a chance to pass a different one.
    void begin(gfx::Image& img, gfx::Color c, std::string label);
    [[nodiscard]] bool        active() const { return active_; }
    [[nodiscard]] std::size_t touched() const { return px_.size(); }

    // Paint one pixel. Out of bounds, a pixel already at the stroke's colour, and a
    // pixel already in this stroke are all ignored.
    void touch(int x, int y);

    // Paint every pixel on the segment from the previous touch to this one. A mouse
    // moving faster than one pixel per frame leaves gaps otherwise, and at the zoom
    // a pixel editor is used at, the pointer moves several image pixels per frame in
    // an ordinary gesture — so a naive per-frame `touch` draws a dotted line.
    void touch_line(int x0, int y0, int x1, int y1);

    // End the gesture. nullopt when nothing was painted, so a click that changed
    // nothing does not become an undo step.
    std::optional<doc::Command> finish();

private:
    gfx::Image*            img_ = nullptr;
    gfx::Color             colour_ = 0;
    std::string            label_;
    std::vector<PixelEdit> px_;
    bool                   active_ = false;
};

} // namespace paint
