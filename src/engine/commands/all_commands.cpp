// =============================================================================
//  engine/commands/all_commands.cpp  —  see all_commands.hpp
// =============================================================================
#include "engine/commands/all_commands.hpp"

#include "engine/commands/asset_commands.hpp"
#include "engine/commands/release_commands.hpp"

namespace cmd {

void register_all(const std::vector<std::string>& known_entries) {
    register_release_commands(known_entries);
    register_asset_commands();
}

} // namespace cmd
