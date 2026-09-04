// =============================================================================
//  games/farm/theme.hpp  —  which picture a tile id wears
// =============================================================================
//  The map stores SEMANTIC ids: ground 1 is grass, ground 2 is the path, decor 1 is
//  a tree. That is what an author edits and what the Studio's palette shows, and it
//  is deliberately not a sheet index — renumbering a map every time the art changes
//  is how a level stops being editable.
//
//  A theme is the join: `<layer> <id> <index>` against one sheet. Swapping art is a
//  new sheet plus, at most, a new theme file — no rebuild, for the same reason the
//  crop table is a text file (D27): art is iterative work, and iterative work should
//  not need a compiler.
//
//  An id with no line here has NO art, and the caller falls back to the flat colour
//  it drew before. That is what makes "support both" per-tile rather than all-or-
//  nothing: an open-licence pack that has no water tile still themes everything else.
//
//  PURE: text in, a lookup out.
// =============================================================================
#pragma once

#include <map>
#include <optional>
#include <string>

namespace farm {

struct Theme {
    std::string sheet;          // asset path of the .hrt to cut
    int         tile = 16;      // pixels per tile in that sheet
    // (layer name, tile id) -> index into the cut sheet
    std::map<std::pair<std::string, int>, int> art;

    // -1 when this id has no art, which is a normal answer, not an error.
    [[nodiscard]] int index_of(const std::string& layer, int id) const;
};

// Parse a theme file. Returns nullopt only when the text is unusable (no sheet, or a
// malformed line) — a theme that maps nothing is legal and means "no art yet".
std::optional<Theme> parse_theme(const std::string& text);

} // namespace farm
