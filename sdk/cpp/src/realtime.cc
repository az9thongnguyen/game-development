// =============================================================================
//  sdk/cpp/src/realtime.cc  —  see gbaas/realtime.h
// =============================================================================
#include "gbaas/realtime.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "gbaas/client.h"
#include "gbaas/json.hpp"

namespace gbaas {

namespace {

// Parse one server frame ("{\"ev\":...}") into an RtEvent. Unknown/extra fields
// are ignored; a malformed frame surfaces as an "error" event (never a throw).
RtEvent parse_event(const std::string& frame) {
    RtEvent e;
    e.raw = frame;
    const auto j = json::parse(frame);
    if (!j) {
        e.ev      = "error";
        e.message = "malformed frame";
        return e;
    }
    const auto& v = *j;
    e.ev      = v["ev"].as_string();
    e.room    = v["room"].as_string();
    e.name    = v["name"].as_string();
    e.data    = v["data"].as_string();
    e.message = v["message"].as_string();
    if (v.has("from"))          e.from = v["from"].as_int();       // msg
    else if (v.has("user_id"))  e.from = v["user_id"].as_int();    // peer_joined / peer_left
    if (v.has("members")) {
        const auto& m = v["members"];
        for (std::size_t k = 0; k < m.size(); ++k) {
            RtPeer p;
            p.user_id = m[k]["user_id"].as_int();
            p.name    = m[k]["name"].as_string();
            e.members.push_back(std::move(p));
        }
    }
    return e;
}

}  // namespace

Realtime::Realtime(Client* client, std::unique_ptr<IWsTransport> transport)
    : client_(client), ws_(std::move(transport)) {}

bool Realtime::connect() {
    if (opened_) return true;
    if (client_->token_.empty()) return false;   // realtime requires an authenticated user
    std::string url = client_->cfg_.base_url;
    if (url.rfind("https://", 0) == 0)     url.replace(0, 5, "wss");   // https -> wss
    else if (url.rfind("http://", 0) == 0) url.replace(0, 4, "ws");    // http  -> ws
    url += "/v1/ws?api_key=" + client_->cfg_.api_key + "&token=" + client_->token_;
    // api_key (pk_...) and the JWT (base64url) are already URL-safe → no encoding.
    opened_        = ws_->open(url);
    was_connected_ = false;
    return opened_;
}

void Realtime::disconnect() {
    if (ws_) ws_->close();
    opened_        = false;
    was_connected_ = false;
    outbox_.clear();
}

bool Realtime::connected() const { return ws_ && ws_->connected(); }

void Realtime::send_op(const std::string& frame) {
    // On web, connect() is async: the socket isn't open yet when a game calls join()
    // right after connect(). Queue such ops and flush them once the socket opens
    // (see update()). On native the handshake is done in connect(), so this sends
    // immediately and the outbox stays empty.
    if (ws_ && ws_->connected()) {
        ws_->send_text(frame);
    } else if (outbox_.size() < 256) {   // ponytail: bounded so a never-open socket can't grow it
        outbox_.push_back(frame);
    }
}

void Realtime::join(const std::string& room) {
    send_op(R"({"op":"join","room":")" + json::escape(room) + R"("})");
}
void Realtime::leave()  { send_op(R"({"op":"leave"})"); }
void Realtime::send(const std::string& data) {
    send_op(R"({"op":"msg","data":")" + json::escape(data) + R"("})");
}
void Realtime::queue()  { send_op(R"({"op":"queue"})"); }
void Realtime::cancel() { send_op(R"({"op":"cancel"})"); }

void Realtime::update() {
    if (!ws_ || !opened_) return;
    if (!was_connected_ && ws_->connected()) {
        was_connected_ = true;
        for (auto& f : outbox_) ws_->send_text(f);   // flush ops queued before open
        outbox_.clear();
        RtEvent e;
        e.ev = "connected";
        events_.push_back(std::move(e));
    }
    std::vector<std::string> frames;
    const bool               alive = ws_->poll(frames);
    for (auto& f : frames) events_.push_back(parse_event(f));
    if (!alive) {
        opened_        = false;
        was_connected_ = false;
        outbox_.clear();
        RtEvent e;
        e.ev = "disconnected";
        events_.push_back(std::move(e));
    }
}

bool Realtime::poll(RtEvent& out) {
    if (events_.empty()) return false;
    out = std::move(events_.front());
    events_.pop_front();
    return true;
}

// Lazily create the single Realtime channel owned by the Client.
Realtime& Client::realtime() {
    if (!rt_) rt_ = std::make_unique<Realtime>(this, make_default_ws_transport());
    return *rt_;
}

}  // namespace gbaas
