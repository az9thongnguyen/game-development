// =============================================================================
//  server/http.hpp  —  HTTP/1.1 request parsing + response building (no sockets)
// =============================================================================
//  Pure functions over byte buffers so they unit-test without a network. net.cpp
//  reads the raw bytes off a socket and hands them here; main.cpp builds Responses
//  and serialize() turns them back into bytes.
// =============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace web {

struct Request {
    std::string method;
    std::string target;   // e.g. "/api/scores?x=1" (raw, incl. query)
    std::string version;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;

    // Case-insensitive header lookup; "" if absent.
    std::string header(const std::string& name) const;
};

struct Response {
    int                  status = 200;
    std::string          reason = "OK";
    std::string          content_type = "text/plain; charset=utf-8";
    std::vector<uint8_t> body;
};

// Parse a full request (header section + however much body was read). Returns
// nullopt for a malformed request line / header block.
std::optional<Request> parse_request(const std::string& raw);

// Serialize status line + headers (Content-Type/Length, Connection: close) + body.
std::vector<uint8_t> serialize(const Response& r);

// Convenience: a text/JSON response from a string body.
Response make_response(int status, const std::string& reason,
                       const std::string& body,
                       const std::string& content_type = "text/plain; charset=utf-8");

} // namespace web
