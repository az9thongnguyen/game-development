// =============================================================================
//  engine/document/document.hpp  —  an editable file, with recovery
// =============================================================================
//  A document is text on disk plus the history of edits to it. Two behaviours that
//  a tool without them punishes you for:
//
//    autosave  — every edit is written to `<path>.autosave` after a quiet interval,
//                so a crash costs seconds rather than a session
//    recovery  — on open, an autosave NEWER than the file is offered rather than
//                silently applied. Silently applying it is how a user loses the
//                version they deliberately saved.
//
//  I/O goes through `assets::`, so this works identically on the web VFS.
// =============================================================================
#pragma once

#include <optional>
#include <string>

#include "engine/document/command_stack.hpp"

namespace doc {

// The path an autosave for `path` lives at. Beside the file, not in a temp
// directory: a recovery that the user cannot find is not a recovery.
std::string autosave_path(const std::string& path);

enum class OpenState {
    Missing,          // no such file
    Clean,            // opened; no autosave present
    RecoveryOffered,  // an autosave exists and differs — ask before using it
};

struct Opened {
    OpenState   state = OpenState::Missing;
    std::string content;            // the saved file's text
    std::string recovered;          // the autosave's text, when one differs
};

// Read a document and look for a newer autosave beside it.
Opened open(const std::string& path);

// Write the autosave. Cheap and idempotent; call it on a timer while dirty.
bool write_autosave(const std::string& path, const std::string& content);

// Save for real and drop the autosave — leaving it behind would offer recovery of
// content the file already contains, which teaches the user to dismiss the prompt.
bool save(const std::string& path, const std::string& content);

// Remove the autosave without saving (the user declined recovery).
bool discard_autosave(const std::string& path);

} // namespace doc
