// =============================================================================
//  games/studio/recipe.hpp  —  TextureParams <-> key=value text (non-destructive)
// =============================================================================
//  A texture is saved with a tiny `.recipe` sidecar so it can be RE-EDITED later:
//  we reload the exact params, not just the flat pixels. Plain key=value text,
//  hand-parsed (no JSON lib) — upgrade to sdk json.hpp only if nesting appears.
// =============================================================================
#pragma once
#include <string>

#include "games/studio/texture_gen.hpp"

namespace studio {

struct TextureRecipe {
    TextureParams texture;
    int           frames = 1;
};

std::string to_recipe(const TextureParams& p);          // deterministic key=value dump
std::string to_recipe(const TextureRecipe& recipe);     // texture keys plus frame count

// Missing and unknown keys keep their default, which is what makes the format
// forward-compatible. But that same tolerance means ANY text parses — an empty file,
// a PNG, last week's shopping list all yield the default texture. `applied` (optional)
// receives the number of keys that were actually recognised, so a caller that is about
// to WRITE something can refuse a file that turned out not to be a recipe at all.
TextureParams from_recipe(const std::string& text, int* applied = nullptr);

// Parse the complete bake recipe. `frames` is clamped to a small, useful range so a
// hand-edited file cannot turn one 128px texture into an accidental multi-megabyte
// sheet. The TextureParams-only API above remains byte-compatible with old Lab saves.
TextureRecipe parse_recipe(const std::string& text, int* applied = nullptr);

} // namespace studio
