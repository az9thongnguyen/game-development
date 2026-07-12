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

} // namespace engine
