// =============================================================================
//  engine/release/ops.hpp  —  release operations as callable, testable functions
// =============================================================================
//  publish / promote / rollback, extracted from main.cpp's CLI so BOTH the CLI and
//  the graphical Hub Scene can invoke them and PRESENT the result themselves (print,
//  or flash in a window). Each returns a structured OpResult instead of writing to
//  stdout — presentation is the caller's job. This is what turns the Hub from a
//  read-only view into a real controller. Pure of SDL; reads/writes go through assets::.
// =============================================================================
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace engine {

// The outcome of a release operation: did it succeed, and one line to show the user.
struct OpResult {
    bool        ok = false;
    std::string message;   // human-readable; CLI prints it, the Scene flashes it
};

// Publish a project's package immutably by content hash and point `channel` at it.
// Idempotent (re-publish identical content is a "verified" no-op); refuses to overwrite
// a release id with different bytes. `known_entries` is the set of launchable entry ids.
OpResult publish(const std::string& project_path, const std::string& channel,
                 const std::string& reason, const std::vector<std::string>& known_entries);

// Move `to` onto the release `from` currently holds (e.g. development → preview). The
// release must exist in the store.
OpResult promote(const std::string& from, const std::string& to, const std::string& reason);

// Point `channel` at an explicit prior release id, which must exist. `release_id` is a
// trust-boundary input (it becomes a path) and is validated before use.
OpResult rollback(const std::string& channel, const std::string& release_id, const std::string& reason);

// The release id a channel currently points at, or nullopt if unset/malformed.
std::optional<std::string> current_release(const std::string& channel);

// The channels with defined promotion semantics: publish lands in development, then
// promote forward to preview (shareable) and production (live). Other names are still
// allowed ad hoc by publish/promote/rollback; these are the ones status reports.
const std::vector<std::string>& well_known_channels();

// ---- reading the store, as data rather than as printed lines ------------------
// status() and log() used to exist only inside main.cpp, formatting straight to
// stdout. A window that wanted the same information had no choice but to reimplement
// the reading — the exact duplication hub_lines exists to prevent one level up. So
// they return values, and every renderer formats them itself.

struct ChannelStatus {
    std::string name;
    std::string release;    // "" when the channel is unset
    bool        present = false;   // is that release actually in the store?
};

// One entry per well-known channel, in pipeline order. Reads the fixed channel files —
// never scans the store directory (the "collection database" smell the strategy warns
// against).
std::vector<ChannelStatus> status();

// The append-only audit history, oldest first, optionally filtered to one channel.
// Malformed lines are skipped rather than aborting the read: a log is evidence, and
// one corrupt line must not hide the rest of it.
struct AuditRecord {
    long long   epoch = 0;
    std::string action, channel, release, prev, reason;
};
std::vector<AuditRecord> log(const std::string& channel_filter = {});

} // namespace engine
