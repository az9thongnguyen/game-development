// =============================================================================
//  engine/mix/mix.hpp  —  a sprite assembled from parts, as TEXT
// =============================================================================
//  The FOURTH door into `.hrt`, and CLAUDE.md's door count changes with it on
//  purpose rather than by accident. The other three answer "where did this picture
//  come from": `asset.import` brings one in from outside, `asset.texture` bakes one
//  from twelve numbers, `asset.pixels` bakes one somebody typed. This one answers a
//  question none of them can: **make me a hundred sprites that all belong together**.
//
//  That is the constraint that makes a parts mixer worth building instead of a
//  brush. Two heads and three bodies is six characters that already share a palette,
//  a silhouette and a pixel grid — and none of the six is a drawing anybody made.
//  A brush gives you freedom and a cast that does not look related; parts give you a
//  cast and take the freedom away, which for one person is the better trade.
//
//      mix1
//      name  farm_anna
//      size  16 16                     the output canvas
//      sheet body textures/parts_farm.hrt 16
//      part  body 0 at 0 0             sheet, tile index, top-left offset
//      part  body 3 at 0 0
//      swap  eaa56c 5b8ec4             exact-RGB recolour of the finished canvas
//
//  `sheet`/`part` are shaped like farm/theme.def's records on purpose: the same two
//  facts (where the pixels live, which tile of them) in the same order, so somebody
//  who can read one can read the other.
//
//  PURE: text in, an Image out, and the images come from a resolver the caller
//  supplies — no I/O and no renderer, exactly like the other three doors. That is
//  what lets a test compose from pixels it made up rather than from files on a disk.
// =============================================================================
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "engine/image.hpp"

namespace mix {

struct Mix {
    struct Sheet { std::string name, path; int tile = 16; };
    struct Part  { std::string sheet; int index = 0; int x = 0, y = 0; };
    struct Swap  { gfx::Color from = 0, to = 0; };

    std::string        name;
    int                w = 0, h = 0;
    std::vector<Sheet> sheets;
    std::vector<Part>  parts;
    std::vector<Swap>  swaps;

    [[nodiscard]] const Sheet* sheet(const std::string& n) const;
};

// The largest canvas a mix may declare. A sprite sheet, not a wallpaper — and a
// number here is what stops a typo in `size` from asking for a gigabyte.
inline constexpr int kMaxSide = 1024;

// Parse. nullopt on any problem, with `why` (when non-null) naming what and where.
//
// Deliberately strict, for the same reason `.pix` is: a composition format whose
// errors are silent produces a picture that is *nearly* right, which is the hardest
// kind of wrong to see. Every one of these is refused rather than defaulted:
//   * a missing `mix1`, a missing or duplicate `size`/`name`, an unknown record
//   * a sheet declared twice, or a `part` naming a sheet that was never declared
//   * a bad colour in `swap`, or a swap of a colour to itself
//   * NO parts at all — a mix that composes nothing is a file somebody abandoned
std::optional<Mix> parse_mix(const std::string& text, std::string* why = nullptr);

// Write it back. Round-trips: to_text(parse_mix(t)) == t for anything this wrote.
std::string to_text(const Mix& m);

// Compose. `find` resolves a declared sheet NAME to its pixels; the caller does the
// reading. Parts are drawn in declaration order, alpha-over, then every swap is
// applied to the finished canvas.
//
// Refused here rather than in the parser, because these need the images:
//   * a sheet whose image the caller could not supply
//   * a part index outside its sheet's grid
//   * a part that lands ENTIRELY off the canvas — a part you cannot see is a typo,
//     not a composition
std::optional<gfx::Image> compose(
    const Mix& m,
    const std::function<const gfx::Image*(const std::string&)>& find,
    std::string* why = nullptr);

// How many tiles of `tile` px fit in an image, row-major. 0 when the image cannot
// hold one — which is the answer that makes "index out of range" checkable.
int tile_count(const gfx::Image& img, int tile);

} // namespace mix
