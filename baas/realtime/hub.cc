// =============================================================================
//  baas/realtime/hub.cc  —  see hub.h
// =============================================================================
#include "baas/realtime/hub.h"

#include <algorithm>
#include <memory>

#include <json/json.h>

namespace web::rt {

namespace {

// Serialize a JSON value to a compact string and push it as a text frame.
void send_json(const drogon::WebSocketConnectionPtr& conn, const Json::Value& v) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";   // one line, no whitespace
    conn->send(Json::writeString(b, v));
}

// The connection's attached metadata (null before auth — callers guard).
std::shared_ptr<ConnMeta> meta_of(const drogon::WebSocketConnectionPtr& conn) {
    return conn->getContext<ConnMeta>();
}

Json::Value peer_obj(const ConnMeta& m) {
    Json::Value o;
    o["user_id"] = static_cast<Json::Int64>(m.user_id);
    o["name"]    = m.display_name;
    return o;
}

}  // namespace

RealtimeHub& RealtimeHub::instance() {
    static RealtimeHub hub;
    return hub;
}

std::string RealtimeHub::room_key(long project_id, const std::string& room) const {
    return std::to_string(project_id) + ":" + room;
}

// Remove `conn` from whatever room it is in and tell the remaining peers.
void RealtimeHub::leave_room_locked(const drogon::WebSocketConnectionPtr& conn, ConnMeta& m) {
    if (m.room.empty()) return;
    const std::string key = room_key(m.project_id, m.room);
    auto it = rooms_.find(key);
    if (it != rooms_.end()) {
        it->second.erase(conn);
        Json::Value ev;
        ev["ev"]      = "peer_left";
        ev["user_id"] = static_cast<Json::Int64>(m.user_id);
        for (const auto& c : it->second) send_json(c, ev);
        if (it->second.empty()) rooms_.erase(it);   // don't leak empty rooms
    }
    m.room.clear();
}

// Move `conn` into `room` (leaving any current room first). When `notify`, tell
// the joiner who's already here (joined) and tell the others a peer arrived.
void RealtimeHub::add_to_room_locked(const drogon::WebSocketConnectionPtr& conn, ConnMeta& m,
                                     const std::string& room, bool notify) {
    leave_room_locked(conn, m);
    m.room = room;
    auto& members = rooms_[room_key(m.project_id, room)];
    members.insert(conn);

    if (!notify) return;

    // members list (everyone now in the room, including the joiner)
    Json::Value joined;
    joined["ev"]      = "joined";
    joined["room"]    = room;
    joined["members"] = Json::Value(Json::arrayValue);
    for (const auto& c : members) {
        if (auto cm = meta_of(c)) joined["members"].append(peer_obj(*cm));
    }
    send_json(conn, joined);

    // tell the existing peers a new one arrived
    Json::Value peer;
    peer["ev"]      = "peer_joined";
    peer["user_id"] = static_cast<Json::Int64>(m.user_id);
    peer["name"]    = m.display_name;
    for (const auto& c : members) {
        if (c != conn) send_json(c, peer);
    }
}

void RealtimeHub::dequeue_locked(const drogon::WebSocketConnectionPtr& conn, ConnMeta& m) {
    if (!m.queued) return;
    auto it = queue_.find(m.project_id);
    if (it != queue_.end()) {
        auto& q = it->second;
        q.erase(std::remove(q.begin(), q.end(), conn), q.end());
        if (q.empty()) queue_.erase(it);
    }
    m.queued = false;
}

void RealtimeHub::join(const drogon::WebSocketConnectionPtr& conn, const std::string& room) {
    auto m = meta_of(conn);
    if (!m) return;
    std::lock_guard<std::mutex> lk(mu_);
    add_to_room_locked(conn, *m, room, /*notify=*/true);
}

void RealtimeHub::leave(const drogon::WebSocketConnectionPtr& conn) {
    auto m = meta_of(conn);
    if (!m) return;
    std::lock_guard<std::mutex> lk(mu_);
    leave_room_locked(conn, *m);
}

void RealtimeHub::broadcast_msg(const drogon::WebSocketConnectionPtr& conn, const std::string& data) {
    auto m = meta_of(conn);
    if (!m) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (m->room.empty()) {
        Json::Value err;
        err["ev"]      = "error";
        err["message"] = "not in a room";
        send_json(conn, err);
        return;
    }
    auto it = rooms_.find(room_key(m->project_id, m->room));
    if (it == rooms_.end()) return;
    Json::Value ev;
    ev["ev"]   = "msg";
    ev["from"] = static_cast<Json::Int64>(m->user_id);
    ev["name"] = m->display_name;
    ev["data"] = data;
    for (const auto& c : it->second) {
        if (c != conn) send_json(c, ev);   // don't echo to the sender
    }
}

void RealtimeHub::enqueue(const drogon::WebSocketConnectionPtr& conn) {
    auto m = meta_of(conn);
    if (!m) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (m->queued) return;   // idempotent
    auto& q = queue_[m->project_id];
    q.push_back(conn);
    m->queued = true;

    // Pair up while at least two wait (FIFO, two-per-match).
    while (q.size() >= 2) {
        const auto a = q[0];
        const auto b = q[1];
        q.erase(q.begin(), q.begin() + 2);
        auto ma = meta_of(a);
        auto mb = meta_of(b);
        if (!ma || !mb) continue;
        ma->queued = false;
        mb->queued = false;
        const std::string room = "match_" + std::to_string(++match_counter_);
        add_to_room_locked(a, *ma, room, /*notify=*/false);
        add_to_room_locked(b, *mb, room, /*notify=*/false);
        Json::Value matched;
        matched["ev"]   = "matched";
        matched["room"] = room;
        send_json(a, matched);
        send_json(b, matched);
    }
    if (q.empty()) queue_.erase(m->project_id);
}

void RealtimeHub::cancel(const drogon::WebSocketConnectionPtr& conn) {
    auto m = meta_of(conn);
    if (!m) return;
    std::lock_guard<std::mutex> lk(mu_);
    dequeue_locked(conn, *m);
}

void RealtimeHub::on_disconnect(const drogon::WebSocketConnectionPtr& conn) {
    auto m = meta_of(conn);
    if (!m) return;
    std::lock_guard<std::mutex> lk(mu_);
    dequeue_locked(conn, *m);
    leave_room_locked(conn, *m);
}

std::size_t RealtimeHub::room_size(long project_id, const std::string& room) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = rooms_.find(room_key(project_id, room));
    return it == rooms_.end() ? 0 : it->second.size();
}

std::size_t RealtimeHub::queue_size(long project_id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = queue_.find(project_id);
    return it == queue_.end() ? 0 : it->second.size();
}

void RealtimeHub::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    rooms_.clear();
    queue_.clear();
    match_counter_ = 0;
}

}  // namespace web::rt
