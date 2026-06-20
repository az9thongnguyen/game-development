// =============================================================================
//  server/mime.hpp  —  filename extension → Content-Type (header-only)
// =============================================================================
//  Just enough types to serve the WASM bundle correctly. The important one is
//  `.wasm` → "application/wasm": browsers refuse to streaming-compile WebAssembly
//  delivered with the wrong MIME type, so getting this right is what makes the
//  game load at all.
// =============================================================================
#pragma once

#include <cctype>
#include <string>

namespace web {

inline std::string mime_for(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    std::string e = (dot == std::string::npos) ? "" : path.substr(dot + 1);
    for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (e == "html" || e == "htm")  return "text/html; charset=utf-8";
    if (e == "js"   || e == "mjs")  return "application/javascript";
    if (e == "wasm")                return "application/wasm";
    if (e == "json")                return "application/json";
    if (e == "css")                 return "text/css; charset=utf-8";
    if (e == "png")                 return "image/png";
    if (e == "jpg"  || e == "jpeg") return "image/jpeg";
    if (e == "svg")                 return "image/svg+xml";
    if (e == "txt")                 return "text/plain; charset=utf-8";
    return "application/octet-stream";   // .data and everything else
}

} // namespace web
