// =============================================================================
//  engine/commands/creature_commands.hpp  —  see the .cpp
// =============================================================================
#pragma once

namespace cmd {

// Registers `creature.verify`. Idempotent, like every other family.
void register_creature_commands();

} // namespace cmd
