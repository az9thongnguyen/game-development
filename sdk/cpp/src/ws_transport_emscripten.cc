// =============================================================================
//  sdk/cpp/src/ws_transport_emscripten.cc  —  web WebSocket transport
// =============================================================================
//  The web counterpart of ws_transport_curl.cc. Uses Emscripten's WebSocket API
//  (emscripten/websocket.h), which maps to the browser's native WebSocket. Opening
//  is asynchronous — connected() flips true from the onopen callback — matching the
//  non-blocking, frame-pumped contract. Received text frames land in an inbox on
//  the browser event loop; poll() drains it. Link with `-lwebsocket.js` (set by
//  CMake for the EMSCRIPTEN build).
// =============================================================================
#include "gbaas/ws_transport.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <emscripten/websocket.h>

namespace gbaas {
namespace {

class WsTransportEmscripten : public IWsTransport {
public:
    ~WsTransportEmscripten() override { close(); }

    bool open(const std::string& url) override {
        close();
        if (!emscripten_websocket_is_supported()) return false;
        EmscriptenWebSocketCreateAttributes attr;
        emscripten_websocket_init_create_attributes(&attr);
        attr.url = url.c_str();
        sock_    = emscripten_websocket_new(&attr);
        if (sock_ <= 0) return false;
        alive_ = true;
        emscripten_websocket_set_onopen_callback(sock_, this, on_open);
        emscripten_websocket_set_onmessage_callback(sock_, this, on_message);
        emscripten_websocket_set_onclose_callback(sock_, this, on_close);
        emscripten_websocket_set_onerror_callback(sock_, this, on_error);
        return true;
    }

    void close() override {
        if (sock_ > 0) {
            emscripten_websocket_close(sock_, 1000, "bye");
            emscripten_websocket_delete(sock_);
            sock_ = 0;
        }
        connected_ = false;
        alive_     = false;
        inbox_.clear();
    }

    bool connected() const override { return connected_; }

    bool send_text(const std::string& text) override {
        if (sock_ <= 0 || !connected_) return false;
        return emscripten_websocket_send_utf8_text(sock_, text.c_str()) ==
               EMSCRIPTEN_RESULT_SUCCESS;
    }

    bool poll(std::vector<std::string>& out) override {
        for (auto& s : inbox_) out.push_back(std::move(s));
        inbox_.clear();
        return alive_;
    }

private:
    static EM_BOOL on_open(int, const EmscriptenWebSocketOpenEvent*, void* ud) {
        auto* self       = static_cast<WsTransportEmscripten*>(ud);
        self->connected_ = true;
        self->alive_     = true;
        return EM_TRUE;
    }
    static EM_BOOL on_message(int, const EmscriptenWebSocketMessageEvent* e, void* ud) {
        auto* self = static_cast<WsTransportEmscripten*>(ud);
        if (e->isText && e->numBytes > 0) {
            // numBytes includes the NUL terminator for text messages.
            self->inbox_.emplace_back(reinterpret_cast<const char*>(e->data),
                                      static_cast<std::size_t>(e->numBytes - 1));
        }
        return EM_TRUE;
    }
    static EM_BOOL on_close(int, const EmscriptenWebSocketCloseEvent*, void* ud) {
        auto* self       = static_cast<WsTransportEmscripten*>(ud);
        self->connected_ = false;
        self->alive_     = false;
        return EM_TRUE;
    }
    static EM_BOOL on_error(int, const EmscriptenWebSocketErrorEvent*, void* ud) {
        static_cast<WsTransportEmscripten*>(ud)->alive_ = false;
        return EM_TRUE;
    }

    EMSCRIPTEN_WEBSOCKET_T   sock_      = 0;
    bool                     connected_ = false;
    bool                     alive_     = false;
    std::vector<std::string> inbox_;
};

}  // namespace

std::unique_ptr<IWsTransport> make_default_ws_transport() {
    return std::make_unique<WsTransportEmscripten>();
}

}  // namespace gbaas
