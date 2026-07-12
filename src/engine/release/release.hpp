// =============================================================================
//  engine/release/release.hpp  —  local immutable release store (Horizon 1)
// =============================================================================
//  The architecture wants a *release* to be immutable: once bytes are published
//  under their content hash, that release id never means anything else, and a
//  channel (preview / production) is just a movable pointer at one of them.
//  Promote = repoint a channel forward; rollback = repoint it back. This is the
//  pure layer of that: how a package hash maps to a store path, the channel
//  pointer format, and the trust-boundary validators. No assets::, no SDL, no
//  I/O — main.cpp does the reading/writing through the assets:: seam.
//  ponytail: a local content-addressed store to learn the mechanics; the full
//  vision's server-side registry (S3/DB) slots behind the same path scheme.
// =============================================================================
#pragma once
#include <optional>
#include <string>

namespace engine {

// The content-addressed directory a package's immutable manifest lives in:
// "releases/<hash>". The hash is the package hash hex from resource.hpp.
std::string release_dir(const std::string& hash_hex);

// The immutable package manifest path inside a release directory:
// "releases/<hash>/package.txt". Writing the same content here is idempotent;
// the same hash never maps to different bytes.
std::string release_manifest_path(const std::string& hash_hex);

// A named channel's pointer-file path: "channels/<name>".
std::string channel_path(const std::string& name);

// A channel file is exactly one line: "channel1 <hash>\n". Serialize / parse it.
std::string serialize_channel(const std::string& hash_hex);
std::optional<std::string> parse_channel(const std::string& text);  // nullopt on bad magic/format

// Trust-boundary validators: a channel name and a release id both become path
// components (rollback/promote take them as arguments), so reject anything that
// could escape the store or malform a path BEFORE building a path from it.
bool valid_channel_name(const std::string& name);  // nonempty, <=64, [A-Za-z0-9_-] only
bool valid_hash_hex(const std::string& hex);        // exactly 16 lowercase hex chars

// The append-only audit log every channel move is recorded in — a *log*, read
// forward, never a directory scan. One line per publish / promote / rollback.
std::string audit_log_path();   // "releases/audit.log"

// One audit record: who moved which channel, to what, from what, and why.
struct AuditEntry {
    long long   epoch = 0;   // seconds since the Unix epoch (0 in tests is fine)
    std::string action;      // "publish" | "promote" | "rollback"
    std::string channel;     // the channel that moved
    std::string release;     // the release id it now points at (16-hex)
    std::string prev;        // predecessor id it pointed at before ("" if unset)
    std::string reason;      // free operator text (may be empty; may contain spaces)
};

// Canonical one line + '\n': "<epoch> <action> <channel> <release> <prev|-> <reason>".
// A missing predecessor is written as "-"; the reason is the rest of the line.
std::string audit_line(const AuditEntry& e);

// Parse one audit line back. nullopt if malformed (fewer than 5 fields, or a field
// that should be a release id is not 16-hex) — the same fail-closed contract as parse_channel.
std::optional<AuditEntry> parse_audit_line(const std::string& line);

} // namespace engine
