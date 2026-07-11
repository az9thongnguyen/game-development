// =============================================================================
//  games/sandbox/serialize.hpp  —  World <-> text (save/load AND Play/Stop snapshot)
// =============================================================================
//  One tolerant, hand-parsed text format, used three ways: write a scene to disk,
//  read it back, and — because a snapshot is just a scene in a string — restore the
//  pre-Play state when the user presses Stop. Grammar (see the design spec §6):
//
//    sandbox1
//    bounds W H
//    e x=F y=F rot=F  color=HEX [round] w=F h=F [tag=I] [mover=F,F] [spinner=F]
//                     [bouncer] [lifetime=F] [spawner=INTERVAL:[archetype]]
//                     [onoverlap=OTHERTAG:(self|other|spawn:[archetype])]
//
//  Unknown tokens are ignored; missing ones fall back to Archetype defaults.
// =============================================================================
#pragma once
#include <string>

#include "games/sandbox/world.hpp"

namespace sandbox {

std::string to_scene(const World& w);
World       from_scene(const std::string& text);

// Archetype <-> its space-joined token list (no surrounding brackets). Exposed for
// tests and reuse by the entity/proto encoders.
std::string archetype_tokens(const Archetype& a);
Archetype   parse_archetype(const std::string& tokens);

} // namespace sandbox
