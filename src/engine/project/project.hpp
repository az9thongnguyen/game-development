// =============================================================================
//  engine/project/project.hpp  —  the game.project manifest (Horizon 0 spine)
// =============================================================================
//  A project is the versioned root of every product action: it names the game
//  and which entry scene launches it, so a build can be selected by a manifest
//  file instead of a hard-coded CLI branch in main.cpp. Pure + headless: parse,
//  validate, and round-trip the text form with no SDL, no assets, no window.
//
//  Format (text, "gameproject1" magic + key/value lines):
//      gameproject1
//      name Creator Demo
//      schema 1
//      entry fps
//
//  Unknown keys are ignored so later additive fields (assets, build profiles,
//  backend endpoint...) stay backward-compatible. ponytail: minimal on purpose —
//  extend the struct when a consumer actually needs a field, not before.
// =============================================================================
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace engine {

// The highest manifest schema this build understands. Bump when a change is not
// purely additive; validate() rejects anything newer with an actionable error.
inline constexpr int kProjectSchema = 1;

struct Project {
    std::string name;         // display name (required)
    int         schema = 0;   // manifest schema version (required, <= kProjectSchema)
    std::string entry;        // entry scene id, e.g. "fps" (required)
};

// Parse the text form. Returns nullopt only on a syntactic failure that makes the
// manifest unusable: a wrong/absent "gameproject1" magic, or a malformed schema
// value. Missing fields parse to empty/zero and are reported by validate() instead.
std::optional<Project> parse_project(const std::string& text);

// Serialize back to the canonical text form (stable round-trip with parse_project).
std::string to_text(const Project& p);

// Semantic checks. Returns one human-readable message per problem; empty = valid.
// known_entries is the set of entry ids this build can actually launch, passed in
// so this core stays free of scene/SDL knowledge.
std::vector<std::string> validate(const Project& p,
                                  const std::vector<std::string>& known_entries);

} // namespace engine
