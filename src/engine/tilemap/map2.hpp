// =============================================================================
//  engine/tilemap/map2.hpp  —  the shared 2D tile map
// =============================================================================
//  Before this there were TWO independent tile grids and neither could be reused:
//  `fps::Map` (a dense uint8 grid with text I/O, living inside games/fps) and
//  `iso::TileMap` (a dense Terrain enum with no text form of its own). Neither has
//  layers, a collision mask, triggers, or any notion of an entity — so no second
//  game could be authored without inventing a third format.
//
//  `map2` is one format with the pieces a 2D game actually needs:
//    - named LAYERS, drawn in declaration order, either tile ids or a 0/1 mask
//    - ENTITIES: a named point with free-form key=value properties
//    - TRIGGERS: a named rectangle with the same properties
//    - TILESET references, resolved by the renderer, not by this module
//
//  PURE: no I/O, no SDL, no renderer. `load()` takes text and `to_text()` produces
//  it; the caller moves the bytes through the `assets::` seam.
//
//  Tile ids are int32, not uint8. fpsmap1's 255-tile ceiling is invisible right up
//  until a tileset crosses it, and widening a format later means another migration.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tilemap {

// Version 2 added per-layer autotile RULES (chapter 134). A file is written at the
// LOWEST version that can express it, so a map with no rules is still a v1 file and
// its bytes — and therefore its release id — do not move for a feature it does not
// use. The guard below (`version > kFormatVersion` refuses) is what makes that
// meaningful: an old binary must refuse a file with rules rather than drop them and
// write the loss back.
inline constexpr int kFormatVersion = 2;

// A layer is either painted tile ids or a boolean mask (collision, water, …).
// Keeping them one type rather than two keeps the file grammar and the row parser
// single, and a mask is just a layer whose ids are 0 or 1.
enum class LayerKind { Tiles, Mask };

// How a MATERIAL tiles. A cell whose value has a rule is not a tile id — it is a
// material, and which piece of its set the cell wears is decided by its neighbours.
// `None` is the absence of a rule and is never written to a file.
//
//   Line — a road: one tile wide, four cardinal neighbours, 16 pieces
//   Blob — a region: grass, a lake, a plateau; eight neighbours, 47 pieces
//
// Which of the two a material is, is a fact about the WORLD, not about the artwork —
// so it lives in the map, where an editor and every renderer can both read it. It
// used to live in the farm's theme file, where the editor could not see it and drew
// a flat colour for a road it had no idea was a road.
enum class RuleKind { None, Line, Blob };

struct Rule {
    std::int32_t value = 0;                  // the material id, never 0 (0 = empty)
    RuleKind     kind  = RuleKind::None;
};

struct Layer {
    std::string          name;
    LayerKind            kind = LayerKind::Tiles;
    std::string          tileset;          // empty for a mask
    std::vector<std::int32_t> cells;       // row-major, w*h; 0 = empty
    // At most one rule per value — a second is a contradiction, not an override, and
    // the parser refuses it. Order is authoring order and is preserved by to_text.
    std::vector<Rule>    rules;

    [[nodiscard]] RuleKind rule_for(std::int32_t value) const;
};

struct Property { std::string key, value; };

struct Entity {
    std::string           name;
    int                   x = 0, y = 0;    // tile coordinates
    std::vector<Property> props;
};

struct Trigger {
    std::string           name;
    int                   x = 0, y = 0, w = 1, h = 1;   // tile rect
    std::vector<Property> props;
};

struct TilesetRef { std::string name, path; };

struct Map {
    std::string             name;
    int                     w = 0, h = 0;
    int                     tile = 16;      // pixels per tile
    std::vector<TilesetRef> tilesets;
    std::vector<Layer>      layers;
    std::vector<Entity>     entities;
    std::vector<Trigger>    triggers;

    // ---- queries ------------------------------------------------------------
    bool in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }

    [[nodiscard]] RuleKind rule_for(const std::string& layer_name, std::int32_t value) const;

    const Layer* layer(const std::string& n) const;
    Layer*       layer(const std::string& n);

    // Tile id at (x,y) in a named layer. Out of bounds or unknown layer -> 0, so a
    // renderer can walk a viewport that overhangs the map without special cases.
    std::int32_t at(const std::string& layer_name, int x, int y) const;
    void         set(const std::string& layer_name, int x, int y, std::int32_t id);

    // Blocked for movement. Out of bounds is SOLID: a world with no wall around it
    // is a world an actor walks out of, and every caller would otherwise repeat the
    // same bounds check. With no collision layer, nothing is solid.
    bool solid(int x, int y) const;

    // Triggers whose rect overlaps the given tile rect, in declaration order.
    std::vector<const Trigger*> triggers_at(int x, int y, int w, int h) const;

    const Entity* entity(const std::string& n) const;
};

// Read a map. Accepts `map2` and — transparently — the older `fpsmap1` written by
// the raycaster and Map Lab, which is migrated on the way in. One entry point means
// no caller has to know which era a file came from.
std::optional<Map> load(const std::string& text);

// Write `map2`. Round-trips: to_text(load(t)) == t for any text this wrote.
std::string to_text(const Map& m);

// The migration on its own, for tests and for tools that want to be explicit.
std::optional<Map> from_fpsmap1(const std::string& text);

// The eight neighbours of (x,y) on `layer` that hold the SAME value it does, as the
// bit mask autotile.hpp reads (kN, kNE, … clockwise from north). Out of bounds is
// NOT the same — a road that reaches the map edge gets an end cap, because that is
// the truth; the alternative pretends the world continues.
std::uint8_t neighbour_mask(const Map& m, const std::string& layer, int x, int y);

// Which piece of its set the cell at (x,y) wears. **0 when its value has no rule**,
// so a caller can always write `base + rule_piece(...)` without asking first.
//
// This is the one implementation. It used to be `farm::line_piece`, a copy that only
// knew about lines and lived where no editor could call it — so the Map workspace
// drew a flat square for a road and you found out what it looked like by running the
// game.
int rule_piece(const Map& m, const std::string& layer, int x, int y);

// Look up a property, or `fallback` when absent.
std::string prop(const std::vector<Property>& props, const std::string& key,
                 const std::string& fallback = {});

} // namespace tilemap
