// =============================================================================
//  gbaas/realtime.h  —  the SDK's realtime (Lobby + Matchmaking) handle
// =============================================================================
//  A persistent, non-blocking WebSocket channel to /v1/ws. Unlike the REST
//  handles (throwaway value types), Realtime is long-lived and owned by Client:
//  get it with client.realtime(). Calls queue frames; received server events are
//  parsed and buffered, then drained with poll() — all pumped by Client::update().
//
//    auto& rt = client.realtime();
//    rt.connect();                       // after a successful auth (needs the token)
//    rt.join("lobby-1");                 // or rt.queue() for matchmaking
//    // each frame:
//    client.update();
//    gbaas::RtEvent ev;
//    while (rt.poll(ev)) { ... switch on ev.ev ... }
// =============================================================================
#pragma once

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "gbaas/ws_transport.h"

namespace gbaas {

class Client;

struct RtPeer {
    long long   user_id = 0;
    std::string name;
};

// A parsed server event. `ev` is the discriminator:
//   "connected"    — the socket is open (synthesized locally)
//   "joined"       — you joined `room`; `members` lists everyone now in it
//   "peer_joined"  — `from`/`name` joined your room
//   "peer_left"    — `from` left your room
//   "msg"          — `from`/`name` broadcast `data`
//   "matched"      — matchmaking put you in `room`
//   "error"        — `message` explains what went wrong
//   "disconnected" — the socket dropped (synthesized locally)
struct RtEvent {
    std::string          ev;
    std::string          room;       // joined / matched
    long long            from = 0;   // msg sender, or peer_joined/left user_id
    std::string          name;       // sender / peer display name
    std::string          data;       // msg payload (opaque to the SDK)
    std::string          message;    // error text
    std::vector<RtPeer>  members;    // joined
    std::string          raw;        // the original frame (for game-specific fields)
};

class Realtime {
public:
    Realtime(Client* client, std::unique_ptr<IWsTransport> transport);

    // Open the socket using the client's base_url + api_key + current token.
    // Idempotent; returns false if there is no token yet or the open failed.
    bool connect();
    void disconnect();
    bool connected() const;

    // ---- ops (queued; sent over the socket) ----
    void join(const std::string& room);
    void leave();
    void send(const std::string& data);   // broadcast to your room
    void queue();                         // enter matchmaking
    void cancel();                        // leave matchmaking

    // Pump the transport → parse frames into events. Called by Client::update().
    void update();

    // Drain one buffered event; returns false when none remain.
    bool poll(RtEvent& out);

private:
    void send_op(const std::string& frame);

    Client*                       client_;
    std::unique_ptr<IWsTransport> ws_;
    std::deque<RtEvent>           events_;
    std::vector<std::string>      outbox_;   // ops sent before the socket opened (web: async connect)
    bool                          opened_        = false;
    bool                          was_connected_ = false;
};

}  // namespace gbaas
