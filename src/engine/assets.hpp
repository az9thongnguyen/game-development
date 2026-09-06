// =============================================================================
//  engine/assets.hpp  —  the asset I/O seam
// =============================================================================
//  ALL file reading goes through this one place. Why: at M5 the web build serves
//  files from a virtual filesystem (Emscripten preloads them), and concentrating
//  I/O here means we adjust loading in ONE spot instead of hunting down scattered
//  fopen/ifstream calls. We intentionally use standard C++ file I/O (not SDL):
//  Emscripten exposes preloaded files through the normal file API, so the same
//  code works on desktop and web, and SDL stays confined to the platform backend.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace assets {

// Directory that relative asset paths are resolved against (default ".").
void set_base_path(const std::string& base);

// Read an entire file as raw bytes. Returns std::nullopt if it can't be opened.
std::optional<std::vector<uint8_t>> load_file(const std::string& path);

// Write raw bytes to a file (truncating any existing content), resolved against
// the base path exactly like load_file. Returns false if the file can't be
// opened or the write fails. This is the SAVE half of the I/O seam — keeping it
// here means the web build (M5) can redirect persistence to IDBFS/localStorage
// in ONE place, without any caller change.
bool write_file(const std::string& path, const std::vector<uint8_t>& bytes);

// Append raw bytes to a file (creating it, and any parent dirs, if absent). Used for
// append-only logs (e.g. the release audit log). Returns false if the write fails.
bool append_file(const std::string& path, const std::vector<uint8_t>& bytes);

// Atomically move `from` onto `to` (both base-relative; parent dirs of `to` created).
// std::filesystem::rename is atomic on one filesystem — this is how a release manifest
// or channel pointer is published without ever exposing a half-written file. False on error.
bool rename(const std::string& from, const std::string& to);

// The names (not paths) of the entries directly inside `dir` (base-relative) whose
// name ends in `suffix`, sorted. `suffix` empty lists everything.
//
// Sorted, because a caller that turns a directory into a FILE — an index, a package,
// a hash — must not produce different bytes on two machines because readdir felt
// different. A missing directory is an empty list, not an error: "nothing here" is
// the right answer for a collection with no projects in it, and it is also what the
// web build sees for a directory the preload excluded.
//
// This is the only listing door for the same reason load_file is the only reading one:
// std::filesystem works over Emscripten's MEMFS, so one implementation serves both,
// and no caller has to know which filesystem it is standing on.
std::vector<std::string> list_dir(const std::string& dir, const std::string& suffix = "");

// Every file at or below `dir` (base-relative) whose name ends in `suffix`, returned
// as paths relative to the BASE — so a result can be handed straight back to
// load_file — sorted.
//
// list_dir answers "what is in this folder"; this answers "what is in this SUBTREE",
// and the difference is whether a caller can be complete. A provenance ledger that
// listed only `textures/` would have missed `pieces/` and `sprites/` and reported a
// clean sheet — the same shape of hole as chapter 128's preload denylist, where the
// thing that got through was the line nobody added.
//
// Directories are never returned, symlinks are not followed, and a missing directory
// is an empty list, exactly as in list_dir.
std::vector<std::string> list_tree(const std::string& dir, const std::string& suffix = "");

// Last-modified time as implementation-defined ticks (for hot-reload change
// detection), or 0 if the file is missing or the platform can't report it (e.g. the
// web has no filesystem watch). Only meaningful when compared against a prior value.
std::int64_t mtime(const std::string& path);

} // namespace assets
