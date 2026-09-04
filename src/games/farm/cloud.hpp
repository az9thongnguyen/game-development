// =============================================================================
//  games/farm/cloud.hpp  —  which save wins, decided before anything is written
// =============================================================================
//  Cloud save is the first feature in this project that can destroy work. Every
//  other operation either succeeds or refuses; this one can succeed at the wrong
//  thing — overwrite an evening's play with a copy from last week — and look like
//  it worked. So the decision is separated from the transport and pinned by a test
//  before a single byte moves.
//
//  PURE: four numbers in, one verdict out. No SDK, no network, no files. That is
//  what lets the whole table be checked in a unit test rather than by playing on
//  two machines and hoping.
//
//  The bookmark is the whole trick. A save's own hash says WHAT it is; it cannot
//  say whether this machine has touched it since the cloud last agreed. So the
//  local side remembers the version+hash it last agreed on, and every question
//  becomes "changed since we agreed?" — which both sides can answer independently.
//  Without the bookmark, "the bytes differ" is all you know, and the only honest
//  response to that is to ask the player every single time.
//
//  It lives in the game, not in engine/, on purpose: it has ONE consumer today.
//  When Creatures needs it, moving it up is a rename — it is already pure.
// =============================================================================
#pragma once

#include <cstdint>

namespace farm {

// What this machine knows about its own save, and about the last time it and the
// cloud agreed. `synced_version == 0` means "never agreed" — a fresh install.
struct LocalSave {
    bool          present        = false;
    std::uint64_t hash           = 0;   // farm::hash() of the world on disk
    long long     synced_version = 0;   // the cloud version last agreed with
    std::uint64_t synced_hash    = 0;   // ...and the hash agreed on
};

struct RemoteSave {
    bool          present = false;
    long long     version = 0;
    std::uint64_t hash    = 0;          // hash of the downloaded payload
};

enum class Sync {
    InSync,     // nothing to do
    Push,       // local is ahead: upload
    Pull,       // cloud is ahead: download
    Conflict    // both moved since they last agreed — ASK, never guess
};

// The full table. Deliberately total: every combination of the six inputs lands on
// exactly one verdict, and the one case that should be impossible (neither side
// claims to have changed, yet the bytes differ) is reported as a Conflict rather
// than being quietly resolved in someone's favour.
Sync decide_sync(const LocalSave& local, const RemoteSave& remote);

// A short phrase for the HUD. Present tense, because it describes what is about to
// happen, not what did.
const char* sync_text(Sync s);

} // namespace farm
