// =============================================================================
//  gbaas/ws_transport.h  —  the WebSocket transport seam (native vs. web)
// =============================================================================
//  The realtime counterpart of transport.h. Realtime logic (op framing, event
//  parsing) talks only to this interface; two implementations exist — libcurl
//  ws:// (native) and emscripten_websocket (web) — plus a fake for unit tests.
//  Everything is non-blocking after open(): poll() is pumped from the game tick
//  (via Client::update()) and never stalls.
// =============================================================================
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace gbaas {

struct IWsTransport {
    virtual ~IWsTransport() = default;

    // Begin connecting to `url` (ws://host:port/path?query). Native does the
    // one-time handshake here; web returns immediately and reports readiness via
    // connected(). Returns false only if the attempt could not be started.
    virtual bool open(const std::string& url) = 0;

    virtual void close() = 0;
    virtual bool connected() const = 0;

    // Queue a text frame. Returns false if not connected / the send failed.
    virtual bool send_text(const std::string& text) = 0;

    // Drain received text frames into `out`. Returns false if the connection has
    // dropped (the caller should surface a disconnect and stop). Never blocks.
    virtual bool poll(std::vector<std::string>& out) = 0;
};

// Provided by the platform TU (ws_transport_curl.cc / ws_transport_emscripten.cc),
// selected by CMake — mirrors make_default_transport() for HTTP.
std::unique_ptr<IWsTransport> make_default_ws_transport();

}  // namespace gbaas
