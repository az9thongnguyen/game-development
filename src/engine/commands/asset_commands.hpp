// =============================================================================
//  engine/commands/asset_commands.hpp  —  bringing foreign art in
// =============================================================================
//  Two commands, one destination — which is the whole answer to "support both":
//
//      asset.import  <src.png>     <dst.hrt>    art from an open-licence pack
//      asset.texture <src.recipe>  <dst.hrt>    art this project drew in the Studio
//
//  Both end as the SAME format, so everything downstream (the asset cache, the
//  tileset, the manifest's resource closure, the package hash) has one kind of file to
//  think about. A tile the pack shipped and a tile we drew are indistinguishable by
//  the time the renderer sees them, and that is the point.
//
//  Both are OFFLINE steps on purpose. The engine decodes no PNG and runs no noise
//  generator at load: a game ships `.hrt`, which is a header and raw pixels. These
//  exist for the moment a human brings something in — or makes something new.
// =============================================================================
#pragma once

namespace cmd {

// Idempotent: registering twice replaces rather than duplicates.
void register_asset_commands();

} // namespace cmd
