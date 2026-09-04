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
#include <memory>
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

// The platform's transport — libcurl on native, emscripten_fetch on the web, chosen
// by CMake. Client(Config) uses it; it is declared here so a caller that wants to
// DECIDE between this and a fake can hold both in the same variable.
std::unique_ptr<ITransport> make_default_transport();

// A transport that answers every request with a transport failure, immediately but
// still ASYNCHRONOUSLY — the callback fires from poll(), like every other transport,
// so code cannot accidentally depend on being called back inside send().
//
// It exists so that "no backend" is a thing you can CONSTRUCT rather than a thing
// that happens to you. A test that must not open a socket, and a build shipped with
// no server behind it, then take the same path the game already handles.
class OfflineTransport : public ITransport {
public:
    void send(const std::string&, const std::string&, const Headers&, const std::string&,
              HttpDone done) override {
        if (done) pending_.push_back(std::move(done));
    }
    void poll() override {
        auto batch = std::move(pending_);
        pending_.clear();
        for (auto& d : batch) d(HttpResponse{-1, "offline"});
    }
private:
    std::vector<HttpDone> pending_;
};

}  // namespace gbaas
