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

// Last-modified time as implementation-defined ticks (for hot-reload change
// detection), or 0 if the file is missing or the platform can't report it (e.g. the
// web has no filesystem watch). Only meaningful when compared against a prior value.
std::int64_t mtime(const std::string& path);

} // namespace assets
