// =============================================================================
//  gbaas/transport.h  —  the HTTP transport seam (native vs. web)
// =============================================================================
//  Mirrors the engine's platform seam: the SDK logic is transport-agnostic and
//  talks only to this interface. Two implementations exist — libcurl (native) and
//  emscripten_fetch (web) — and the tests use a fake one. Requests are ASYNC: the
//  callback fires later, when poll() is pumped (from the game tick via
//  Client::update()). A body implies Content-Type: application/json.
// =============================================================================
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace gbaas {

struct HttpResponse {
    int         status = 0;   // HTTP status, or -1 on a transport failure
    std::string body;
};

using Headers    = std::vector<std::pair<std::string, std::string>>;
using HttpDone   = std::function<void(HttpResponse)>;

struct ITransport {
    virtual ~ITransport() = default;

    // Start a request. The callback is invoked exactly once, later, from poll().
    virtual void send(const std::string& method, const std::string& url,
                      const Headers& headers, const std::string& body, HttpDone done) = 0;

    // Pump in-flight transfers; fire callbacks for any that completed. Called each
    // frame via Client::update(). Never blocks.
    virtual void poll() = 0;
};

}  // namespace gbaas
