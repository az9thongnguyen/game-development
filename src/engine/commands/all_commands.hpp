// =============================================================================
//  engine/commands/all_commands.hpp  —  one call registers everything
// =============================================================================
//  There were four call sites of `register_release_commands`, and adding a second
//  family of commands would have made eight — with nothing to stop a process from
//  registering one family and not the other, so `--cmd` and the Studio's palette
//  would list different things depending on which flag started the process.
//
//  Same lesson as chapter 120, one layer down: one door, and every caller uses it.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace cmd {

// `known_entries` is the set of launchable entry ids, needed by publish's validation.
// Idempotent: calling it twice replaces the registrations rather than duplicating them.
void register_all(const std::vector<std::string>& known_entries);

} // namespace cmd
