// =============================================================================
//  games/farm/cloud.cpp
// =============================================================================
#include "games/farm/cloud.hpp"

namespace farm {

Sync decide_sync(const LocalSave& local, const RemoteSave& remote) {
    if (!local.present && !remote.present) return Sync::InSync;
    if (!remote.present)                   return Sync::Push;   // local.present is true here
    if (!local.present)                    return Sync::Pull;

    // Content first: two saves that hash the same ARE the same run, whatever the
    // version numbers say. Comparing versions before content would push a byte-identical
    // save just because another machine happened to save it second.
    if (local.hash == remote.hash) return Sync::InSync;

    const bool local_changed  = local.hash != local.synced_hash;
    const bool remote_changed = remote.version != local.synced_version;

    if (local_changed && remote_changed) return Sync::Conflict;
    if (local_changed)                   return Sync::Push;
    if (remote_changed)                  return Sync::Pull;

    // Neither side claims to have moved since they agreed, yet the bytes differ. That
    // means the bookmark is lying — a save was written without the version advancing,
    // or the file was edited underneath us. There is no safe automatic answer, so the
    // player gets asked. Reporting this as InSync would silently keep whichever copy
    // happened to be local.
    return Sync::Conflict;
}

const char* sync_text(Sync s) {
    switch (s) {
        case Sync::Push:     return "uploading";
        case Sync::Pull:     return "downloading";
        case Sync::Conflict: return "two saves differ";
        case Sync::InSync:   break;
    }
    return "in sync";
}

} // namespace farm
