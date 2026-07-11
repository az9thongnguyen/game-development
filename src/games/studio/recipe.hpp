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

std::string   to_recipe(const TextureParams& p);       // deterministic key=value dump
TextureParams from_recipe(const std::string& text);    // missing/unknown keys -> defaults

} // namespace studio
