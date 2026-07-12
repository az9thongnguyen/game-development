// =============================================================================
//  engine/hub/hub_build.hpp  —  assemble a HubView from the project + release store
// =============================================================================
//  The impure half of the hub: it reads (through the assets:: seam) a project manifest,
//  validates it, resolves its content closure, hashes the current source, and reads each
//  channel pointer — filling in a plain HubView. Kept out of the pure hub_core so both
//  the CLI (--hub) and the graphical Hub Scene build the view the same way, from one place.
// =============================================================================
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "engine/hub/hub.hpp"

namespace engine {

// Load + parse + validate + resolve closure for `path`, then read development/preview/
// production. Returns nullopt only if the manifest is unreadable or unparseable — a
// NOT-shippable project is a valid state the hub must show, not a failure. `known_entries`
// is the set of launchable entry ids (kept in the caller so this stays scene-list-free).
std::optional<HubView> build_hub_view(const std::string& path,
                                      const std::vector<std::string>& known_entries);

} // namespace engine
