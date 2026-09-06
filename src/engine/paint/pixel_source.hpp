// =============================================================================
//  engine/paint/pixel_source.hpp  —  hand-drawn art as TEXT, baked to .hrt
// =============================================================================
//  The third door into `.hrt`, and the one the autotile set has been waiting for.
//
//  `asset.import` brings a picture in from outside. `asset.texture` bakes a picture
//  made of twelve numbers — noise with a ramp, which is why the Texture Lab can draw
//  water and cannot draw a road. Chapter 123 found the ceiling the honest way: the
//  farm's path is one tile wide, no pack in hand ships a narrow-strip piece, and no
//  amount of noise makes one. Somebody has to place the pixels.
//
//  The Pixel workspace is where a pixel gets TWEAKED. It is the wrong tool for
//  BRINGING A SET INTO EXISTENCE, and the reason is specific rather than a
//  preference: a 16-piece autotile set is not sixteen drawings. It is one corridor
//  profile and one centre block, cut sixteen ways, and every piece has to agree with
//  every other piece along its seams. What matters is the relationship BETWEEN the
//  pieces, and a relationship is something you read, diff and review — not something
//  you verify by clicking through sixteen canvases.
//
//  So: the source is text, the artefact is `.hrt`, and the relationship is the same
//  one `.recipe` has to a generated texture (chapter 122). Same rule follows: the
//  committed `.hrt` must be what the committed source bakes to, and a test says so
//  rather than the commit message.
//
//      # a comment
//      size 16                  pixels per tile, square
//      grid 4 4                 the sheet is 4 tiles across, 4 down
//      palette . 00000000       one char -> one RGBA colour (rrggbb or rrggbbaa)
//      palette d eaa56c
//      tile 5                   index into the grid, row-major
//      <exactly `size` rows of exactly `size` chars>
//
//  PURE: text in, an Image out. No I/O, no renderer — the command does the reading
//  and the writing, exactly like the other two doors.
// =============================================================================
#pragma once

#include <optional>
#include <string>

#include "engine/image.hpp"

namespace paint {

// Bake an ASCII pixel-art sheet. nullopt on any problem, with `why` (when non-null)
// set to a message naming the LINE — a pixel format whose errors are silent is worse
// than no format, because a mistyped character is a hole nobody looks at twice.
//
// Deliberately strict. Every one of these is refused rather than defaulted:
//   * an unknown record, a duplicate `size`/`grid`/`palette` char, a bad colour
//   * a row of the wrong length, or a tile with the wrong number of rows
//   * a character no `palette` line declared  <- the typo this format must not eat
//   * a tile index out of range, or declared twice
//   * a grid slot left undrawn  <- a hole in an autotile set is not "not yet"
std::optional<gfx::Image> bake_pixels(const std::string& text, std::string* why = nullptr);

// A new, empty sheet as `.pix` TEXT: `cols` x `rows` tiles of `size` px, every pixel
// transparent, every slot present.
//
// Text and not an Image, because a new asset in this project is born as a SOURCE. A
// blank `.hrt` written straight to disk would arrive with no origin — the exact hole
// `engine::scan_provenance` exists to refuse — and would be uneditable as anything
// but pixels. Writing the source first means the file is `drawn` from its first
// second and the ledger needs no special case for "made in the Studio".
//
// Every slot is emitted because the parser refuses a grid with a hole in it, and a
// creation path that produced a file its own parser rejects would be a very short
// bug. Empty when any dimension is < 1 or the sheet would exceed `max_px` on a side.
std::string blank_sheet(int size, int cols, int rows, const std::string& name,
                        int max_px = 4096);

} // namespace paint
