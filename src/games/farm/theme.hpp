// =============================================================================
//  games/farm/theme.hpp  —  which picture a tile id wears
// =============================================================================
//  The map stores SEMANTIC ids: ground 1 is grass, ground 2 is the path, decor 1 is
//  a tree. That is what an author edits and what the Studio's palette shows, and it
//  is deliberately not a sheet index — renumbering a map every time the art changes
//  is how a level stops being editable.
//
//  A theme is the join: `<layer> <id>` -> `<sheet> <index>`. Swapping art is a new
//  sheet plus, at most, a new theme file — no rebuild, for the same reason the crop
//  table is a text file (D27): art is iterative work, and iterative work should not
//  need a compiler.
//
//  SHEETS ARE NAMED, AND THERE CAN BE SEVERAL. That is not generality for its own
//  sake — it is what keeps "support both" honest. The imported CC0 pack is one file
//  with one licence; a tile this project drew in the Texture Lab is ours. Compositing
//  our pixel into someone else's sheet would make the derived file impossible to
//  attribute and would be destroyed the next time the import is re-run. So each
//  source keeps its own `.hrt`, and the theme is the only place they meet.
//
//  An id with no line here has NO art, and the caller falls back to the flat colour
//  it drew before. That is what makes "support both" per-tile rather than all-or-
//  nothing: an open-licence pack that has no water tile still themes everything else,
//  and the tile it lacks arrives later from a different sheet with one added line.
//
//  PURE: text in, a lookup out. `line_piece` at the bottom is the same kind of thing
//  one level up — a map and a cell in, the piece that cell wears out — and it lives
//  here rather than on the scene because "which picture does this cell get" is exactly
//  what this file answers, and because a function on a Scene cannot be checked against
//  a real map without a renderer.
// =============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "engine/tilemap/map2.hpp"

namespace farm {

struct Theme {
    struct Sheet {
        std::string path;        // asset path of the .hrt to cut
        int         tile = 16;   // pixels per tile in that sheet
    };
    struct Art {
        std::string sheet;       // a key of `sheets` — parse_theme refuses any other
        // Into that sheet, cut left-to-right, top-to-bottom. When the MAP gives this
        // id an autotile rule, `index` is the FIRST of the set's consecutive pieces
        // and the one actually drawn is chosen per cell from its neighbours — see
        // tilemap::rule_piece. Which it is, is not written here: whether a material is
        // a road or a region is a fact about the world, and it lives in the map where
        // an editor can see it (chapter 134). The theme says only where the pixels are.
        int index = 0;
    };

    std::map<std::string, Sheet>              sheets;   // name -> where the pixels are
    std::map<std::pair<std::string, int>, Art> art;     // (layer, id) -> which tile

    // nullptr when this id has no art, which is a normal answer, not an error.
    [[nodiscard]] const Art* find(const std::string& layer, int id) const;
};

// Parse a theme file:
//
//     sheet <name> <path> [tile]           declare where pixels come from
//     tile  <layer> <id> <sheet> <index>   join a semantic id to one of them
//
// There used to be an `autotile` record here carrying the same fields; chapter 134
// moved that decision into the map, so a `tile` whose id has a rule is a base and one
// whose id has none is a single picture. An `autotile` line is now an unknown record
// and refused LOUDLY — a theme that still has one has lost nothing silently.
//
// Returns nullopt when the text is unusable: no sheet, a malformed line, an unknown
// record, a duplicate sheet name, a second line for an id already mapped, or a
// `tile` line naming a sheet that was never declared. That last one is the failure
// this format introduced, and it is the one that must not be silent — a typo in a
// sheet name would otherwise read exactly like "this tile has no art yet" and quietly
// lose the art.
//
// A theme that declares a sheet and maps nothing is legal and means "no art yet".
std::optional<Theme> parse_theme(const std::string& text);

// `line_piece` used to live here. It is `tilemap::rule_piece` now — the same neighbour
// scan, in the map module, where the Map workspace can call it too. A copy of it here
// is why the editor drew a flat square for a road it had no idea was a road.

} // namespace farm
