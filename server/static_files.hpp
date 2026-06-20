// =============================================================================
//  server/static_files.hpp  —  safely map a URL path to a file under a root
// =============================================================================
//  Path traversal is the #1 static-server vulnerability: a request for
//  "/../../etc/passwd" must NOT escape the web root. resolve() percent-decodes,
//  rejects any escaping path, and only then joins to the root. Pure of sockets so
//  it is unit-tested directly.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace web {

// Map a request target (may include a "?query") to a safe absolute-ish file path
// under `root`. Returns nullopt if the path would escape the root or is malformed.
// An empty path or "/" maps to the index file ("demo.html").
std::optional<std::string> resolve(const std::string& root, const std::string& target);

// Read a whole file as bytes (nullopt if it can't be opened).
std::optional<std::vector<uint8_t>> read_file(const std::string& path);

} // namespace web
