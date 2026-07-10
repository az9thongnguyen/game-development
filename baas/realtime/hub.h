// =============================================================================
//  baas/realtime/hub.h  —  the in-memory realtime hub (Lobby + Matchmaking)
// =============================================================================
//  The controller (ws_controller.cc) is a thin adapter: it authenticates the
//  upgrade and turns each WebSocket frame into a hub call. ALL shared state — who
//  is in which room, and who is waiting in the matchmaking queue — lives here,
//  behind one mutex, because Drogon dispatches WebSocket callbacks across several
//  event-loop threads.
//
//  Everything is scoped by project_id (the authenticated tenant): rooms are keyed
//  "project:room" and the matchmaking queue is per project, so two games sharing a
//  room name never see each other.
//
//  ponytail: single-node, in-memory, one coarse mutex. A multi-node deploy needs a
//  shared bus (Redis pub/sub) and per-project sharding — the hub's method surface
//  is the seam to swap. Stated, not built; correct for a single-node demo.
// =============================================================================
#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <drogon/WebSocketConnection.h>

namespace web::rt {

// Per-connection state, attached to the WebSocketConnection via setContext once
// the upgrade is authenticated. Mutated only under RealtimeHub's mutex.
struct ConnMeta {
    long        project_id = 0;
    long        user_id    = 0;
    std::string display_name;
    std::string room;              // "" = not in any room
    bool        queued = false;    // currently in the matchmaking queue?
};

class RealtimeHub {
public:
    static RealtimeHub& instance();

    // ---- lobby ----
    void join(const drogon::WebSocketConnectionPtr& conn, const std::string& room);
    void leave(const drogon::WebSocketConnectionPtr& conn);
    void broadcast_msg(const drogon::WebSocketConnectionPtr& conn, const std::string& data);

    // ---- matchmaking ----
    void enqueue(const drogon::WebSocketConnectionPtr& conn);
    void cancel(const drogon::WebSocketConnectionPtr& conn);

    // ---- lifecycle ----
    void on_disconnect(const drogon::WebSocketConnectionPtr& conn);

    // ---- test/inspection (thread-safe snapshots) ----
    std::size_t room_size(long project_id, const std::string& room);
    std::size_t queue_size(long project_id);
    void        reset();   // clears all state (test isolation)

private:
    RealtimeHub() = default;

    // Helpers below assume mu_ is held.
    std::string room_key(long project_id, const std::string& room) const;
    void add_to_room_locked(const drogon::WebSocketConnectionPtr& conn, ConnMeta& m,
                            const std::string& room, bool notify);
    void leave_room_locked(const drogon::WebSocketConnectionPtr& conn, ConnMeta& m);
    void dequeue_locked(const drogon::WebSocketConnectionPtr& conn, ConnMeta& m);

    std::mutex mu_;
    std::map<std::string, std::set<drogon::WebSocketConnectionPtr>> rooms_;   // "pid:room" → conns
    std::map<long, std::vector<drogon::WebSocketConnectionPtr>>     queue_;   // pid → FIFO
    long match_counter_ = 0;
};

}  // namespace web::rt
