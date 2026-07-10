// =============================================================================
//  sdk/cpp/src/ws_transport_curl.cc  —  native WebSocket transport (libcurl ws://)
// =============================================================================
//  Uses libcurl's WebSocket support (>= 7.86): CURLOPT_CONNECT_ONLY=2 does the
//  handshake in open(), then curl_ws_send / curl_ws_recv move frames. The socket
//  is switched to non-blocking so poll() drains whatever is ready and returns.
//
//  Availability is decided by CMake (GBAAS_HAS_WS_CURL): macOS's system libcurl
//  lacks ws://, so the SDK builds against Homebrew's WS-capable curl when present.
//  If none is found, this compiles to a stub that reports "unavailable" — the REST
//  half of the SDK still works; only the realtime channel is inert.
//
//  ponytail: open() does a one-time blocking handshake (negligible on localhost);
//  a fully async connect is the upgrade path if a game ever connects mid-frame.
// =============================================================================
#include "gbaas/ws_transport.h"

#include <memory>
#include <string>
#include <vector>

#ifdef GBAAS_HAS_WS_CURL

#include <fcntl.h>
#include <sys/select.h>
#include <unistd.h>

#include <curl/curl.h>

namespace gbaas {
namespace {

class WsTransportCurl : public IWsTransport {
public:
    ~WsTransportCurl() override { close(); }

    bool open(const std::string& url) override {
        close();
        curl_ = curl_easy_init();
        if (!curl_) return false;
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 2L);   // 2 = WebSocket mode
        if (curl_easy_perform(curl_) != CURLE_OK) { close(); return false; }   // handshake

        // Make the underlying socket non-blocking so poll() never stalls (and so
        // curl_ws_recv drains libcurl's internal buffer, then returns CURLE_AGAIN).
        sock_ = CURL_SOCKET_BAD;
        if (curl_easy_getinfo(curl_, CURLINFO_ACTIVESOCKET, &sock_) == CURLE_OK &&
            sock_ != CURL_SOCKET_BAD) {
            const int fl = ::fcntl(sock_, F_GETFL, 0);
            if (fl != -1) ::fcntl(sock_, F_SETFL, fl | O_NONBLOCK);
        }
        connected_ = true;
        return true;
    }

    void close() override {
        if (curl_) { curl_easy_cleanup(curl_); curl_ = nullptr; }
        sock_      = CURL_SOCKET_BAD;
        connected_ = false;
        pending_.clear();
    }

    bool connected() const override { return connected_; }

    bool send_text(const std::string& text) override {
        if (!curl_ || !connected_) return false;
        // On a non-blocking socket curl_ws_send may return CURLE_AGAIN ("not sent,
        // call again") — it is NOT success. Wait briefly for writability and retry
        // (frames are small + infrequent, so a short bounded wait is fine).
        for (int tries = 0; tries < 100; ++tries) {
            std::size_t    sent = 0;
            const CURLcode r =
                curl_ws_send(curl_, text.data(), text.size(), &sent, 0, CURLWS_TEXT);
            if (r == CURLE_OK) return true;
            if (r != CURLE_AGAIN) { connected_ = false; return false; }
            if (sock_ != CURL_SOCKET_BAD) {   // block up to 5ms for the socket to drain
                fd_set         wr;
                FD_ZERO(&wr);
                FD_SET(sock_, &wr);
                struct timeval tv{0, 5000};
                ::select(static_cast<int>(sock_) + 1, nullptr, &wr, nullptr, &tv);
            }
        }
        return false;   // could not flush after ~0.5s
        // ponytail: small frames send whole; loop on partial sends only if large
        // payloads ever matter.
    }

    bool poll(std::vector<std::string>& out) override {
        if (!curl_ || !connected_) return false;
        for (;;) {
            char                        buf[4096];
            std::size_t                 got  = 0;
            const struct curl_ws_frame* meta = nullptr;
            const CURLcode              r    = curl_ws_recv(curl_, buf, sizeof(buf), &got, &meta);
            if (r == CURLE_AGAIN) return true;                 // nothing more right now
            if (r != CURLE_OK)   { connected_ = false; return false; }   // error/closed
            if (!meta)           continue;
            if (meta->flags & CURLWS_CLOSE) { connected_ = false; return false; }
            if (meta->flags & CURLWS_TEXT) {
                pending_.append(buf, got);
                if (meta->bytesleft == 0) {                    // last fragment of the frame
                    out.push_back(std::move(pending_));
                    pending_.clear();
                }
            }
            // ping/pong/binary frames are ignored (libcurl auto-answers pings).
        }
    }

private:
    CURL*         curl_      = nullptr;
    curl_socket_t sock_      = CURL_SOCKET_BAD;   // the active socket (for select)
    bool          connected_ = false;
    std::string   pending_;   // accumulates a fragmented text frame
};

}  // namespace

std::unique_ptr<IWsTransport> make_default_ws_transport() {
    return std::make_unique<WsTransportCurl>();
}

}  // namespace gbaas

#else  // ------------------------------------------------------------- stub -----

namespace gbaas {
namespace {

class WsTransportStub : public IWsTransport {
public:
    bool open(const std::string&) override { return false; }   // native WS unavailable
    void close() override {}
    bool connected() const override { return false; }
    bool send_text(const std::string&) override { return false; }
    bool poll(std::vector<std::string>&) override { return false; }
};

}  // namespace

std::unique_ptr<IWsTransport> make_default_ws_transport() {
    // No WebSocket-capable libcurl at build time (needs >= 7.86 with curl/websockets.h).
    return std::make_unique<WsTransportStub>();
}

}  // namespace gbaas

#endif
