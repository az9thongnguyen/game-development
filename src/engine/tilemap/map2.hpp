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

inline constexpr int kFormatVersion = 1;

// A layer is either painted tile ids or a boolean mask (collision, water, …).
// Keeping them one type rather than two keeps the file grammar and the row parser
// single, and a mask is just a layer whose ids are 0 or 1.
enum class LayerKind { Tiles, Mask };

struct Layer {
    std::string          name;
    LayerKind            kind = LayerKind::Tiles;
    std::string          tileset;          // empty for a mask
    std::vector<std::int32_t> cells;       // row-major, w*h; 0 = empty
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

// Look up a property, or `fallback` when absent.
std::string prop(const std::vector<Property>& props, const std::string& key,
                 const std::string& fallback = {});

} // namespace tilemap
