// =============================================================================
//  baas/realtime/ws_controller.cc  —  see ws_controller.h
// =============================================================================
#include "baas/realtime/ws_controller.h"

#include <exception>
#include <memory>
#include <string>

#include <json/json.h>

#include "baas/app_config.h"
#include "baas/auth/jwt.h"
#include "baas/db/db.h"
#include "baas/realtime/hub.h"

namespace web::rt {

namespace {

constexpr std::size_t kMaxFrameBytes = 8 * 1024;   // cap inbound frames (bound memory)

void send_json(const drogon::WebSocketConnectionPtr& conn, const Json::Value& v) {
    Json::StreamWriterBuilder b;
    b["indentation"] = "";
    conn->send(Json::writeString(b, v));
}

void send_error(const drogon::WebSocketConnectionPtr& conn, const std::string& msg) {
    Json::Value e;
    e["ev"]      = "error";
    e["message"] = msg;
    send_json(conn, e);
}

// Parse a text frame into a JSON object. Returns false on malformed input.
bool parse_frame(const std::string& s, Json::Value& out) {
    Json::CharReaderBuilder b;
    const std::unique_ptr<Json::CharReader> reader(b.newCharReader());
    std::string errs;
    return reader->parse(s.data(), s.data() + s.size(), &out, &errs) && out.isObject();
}

}  // namespace

void WsController::handleNewConnection(const drogon::HttpRequestPtr& req,
                                       const drogon::WebSocketConnectionPtr& conn) {
    const std::string api_key = req->getParameter("api_key");
    const std::string token   = req->getParameter("token");
    if (api_key.empty() || token.empty()) {
        send_error(conn, "missing api_key or token");
        conn->shutdown();
        return;
    }
    try {
        // Same project lookup as ApiKeyFilter.
        const auto rows =
            db::client()->execSqlSync("SELECT id FROM projects WHERE public_key=?", api_key);
        if (rows.empty()) {
            send_error(conn, "invalid api_key");
            conn->shutdown();
            return;
        }
        const long project_id = rows[0]["id"].as<long>();

        // Same verify + project-match check as AuthFilter.
        const auto claims = jwt::verify(token, config().jwt_secret);
        if (!claims || claims->pid != project_id) {
            send_error(conn, "invalid or expired token");
            conn->shutdown();
            return;
        }

        // Resolve the display name (scoped to the tenant) for peer lists.
        std::string display_name;
        const auto urows = db::client()->execSqlSync(
            "SELECT display_name FROM users WHERE id=? AND project_id=?",
            static_cast<long>(claims->sub), project_id);
        if (!urows.empty() && !urows[0]["display_name"].isNull())
            display_name = urows[0]["display_name"].as<std::string>();

        auto meta          = std::make_shared<ConnMeta>();
        meta->project_id   = project_id;
        meta->user_id      = claims->sub;
        meta->display_name = display_name;
        conn->setContext(meta);
    } catch (const std::exception&) {
        send_error(conn, "auth failed");
        conn->shutdown();
    }
}

void WsController::handleNewMessage(const drogon::WebSocketConnectionPtr& conn,
                                    std::string&& message,
                                    const drogon::WebSocketMessageType& type) {
    if (type != drogon::WebSocketMessageType::Text) return;   // ping/pong handled by Drogon
    if (!conn->getContext<ConnMeta>()) { conn->shutdown(); return; }   // unauthenticated
    if (message.size() > kMaxFrameBytes) { send_error(conn, "frame too large"); return; }

    Json::Value f;
    if (!parse_frame(message, f)) { send_error(conn, "malformed frame"); return; }
    const std::string op = f["op"].asString();

    auto& hub = RealtimeHub::instance();
    if (op == "join") {
        const std::string room = f["room"].asString();
        if (room.empty() || room.size() > 128) { send_error(conn, "invalid room"); return; }
        hub.join(conn, room);
    } else if (op == "leave") {
        hub.leave(conn);
    } else if (op == "msg") {
        hub.broadcast_msg(conn, f["data"].asString());
    } else if (op == "queue") {
        hub.enqueue(conn);
    } else if (op == "cancel") {
        hub.cancel(conn);
    } else {
        send_error(conn, "unknown op");
    }
}

void WsController::handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) {
    RealtimeHub::instance().on_disconnect(conn);
}

}  // namespace web::rt
