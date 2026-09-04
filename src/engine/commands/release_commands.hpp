// =============================================================================
//  engine/commands/release_commands.hpp  —  the platform spine, as commands
// =============================================================================
//  Registers publish / promote / rollback / status / log against the SAME
//  release_ops functions the CLI and the Hub already call. Nothing here
//  reimplements an operation; it only gives each one a stable id and argument
//  parsing, so `--cmd release.promote development preview "why"` and a button in the
//  Studio reach identical code.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cmd {

// `known_entries` is the set of launchable entry ids, needed by publish's validation.
// Idempotent: calling it twice replaces the registrations rather than duplicating them.
void register_release_commands(const std::vector<std::string>& known_entries);

} // namespace cmd
