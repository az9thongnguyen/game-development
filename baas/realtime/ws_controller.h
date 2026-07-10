// =============================================================================
//  baas/realtime/ws_controller.h  —  the /v1/ws WebSocket endpoint
// =============================================================================
//  A Drogon WebSocketController. It authenticates the upgrade request (api_key +
//  token as query params, reusing the exact REST checks), attaches a ConnMeta to
//  the connection, then forwards each JSON frame to the RealtimeHub. All shared
//  state lives in the hub; this class is a thin, stateless adapter.
//
//  Self-registers at startup via the WS_PATH_ADD macro (it is compiled into the
//  baas_core OBJECT library, so the registration runs).
// =============================================================================
#pragma once

#include <drogon/WebSocketController.h>

namespace web::rt {

class WsController : public drogon::WebSocketController<WsController> {
public:
    void handleNewConnection(const drogon::HttpRequestPtr& req,
                             const drogon::WebSocketConnectionPtr& conn) override;
    void handleNewMessage(const drogon::WebSocketConnectionPtr& conn, std::string&& message,
                          const drogon::WebSocketMessageType& type) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr& conn) override;

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/v1/ws");
    WS_PATH_LIST_END
};

}  // namespace web::rt
