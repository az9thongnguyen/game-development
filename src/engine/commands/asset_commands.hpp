// =============================================================================
//  engine/commands/asset_commands.hpp  —  bringing foreign art in
// =============================================================================
//  One command: `asset.import <src.png> <dst.hrt>`. It is the whole answer to
//  "support both" — art from an open-licence pack and art drawn in the Studio end up
//  as the SAME format, so everything downstream (the asset cache, the tileset, the
//  manifest's resource closure, the package hash) has one kind of file to think about.
//
//  Import is an OFFLINE step on purpose. The engine does not decode PNG at runtime:
//  a game ships `.hrt`, which is a header and raw pixels, and the PNG reader exists
//  for the moment a human brings something in.
// =============================================================================
#pragma once

namespace cmd {

// Idempotent: registering twice replaces rather than duplicates.
void register_asset_commands();

} // namespace cmd
