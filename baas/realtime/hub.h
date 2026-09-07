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
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
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

    // ---- who was matched with whom ----
    // The ladder asks. A rated-match report names an opponent and a room, and
    // without this the endpoint would rate any two user ids a client felt like
    // naming — the same hole as letting the client pick the VALUE, one level up.
    // Only the server knows who it paired, and this is where it remembers.
    //
    // ponytail: the last kMatchMemory pairings, in memory, evicted oldest-first, and
    // gone on restart. That matches what the rest of this hub is (single-node,
    // in-memory) and it means a report that arrives after a restart is refused
    // rather than trusted — the safe direction.
    static constexpr std::size_t kMatchMemory = 4096;
    bool was_matched(long project_id, const std::string& room, long a, long b);

    // Record a pairing without going through matchmaking. For tests that exercise
    // the ladder over plain HTTP: four WebSockets to prove an arithmetic rule would
    // be testing the wrong thing, and `creature_pvp_live` drives the real path.
    void remember_match(long project_id, const std::string& room, long a, long b);

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
    void remember_match_locked(long project_id, const std::string& room, long a, long b);

    std::mutex mu_;
    std::map<std::string, std::set<drogon::WebSocketConnectionPtr>> rooms_;   // "pid:room" → conns
    std::map<long, std::vector<drogon::WebSocketConnectionPtr>>     queue_;   // pid → FIFO
    std::map<std::string, std::pair<long, long>> matches_;      // "pid:room" → the pair
    std::deque<std::string>                      match_order_;  // ...oldest first
    long match_counter_ = 0;
};

}  // namespace web::rt
