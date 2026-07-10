// =============================================================================
//  baas/live_events/events_controller.h  —  HTTP edge for /v1/events (read)
// =============================================================================
//  Public read (api-key only): the events active right now. Scheduling is a
//  dashboard (L3) admin action.
// =============================================================================
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace web {

class EventsController : public drogon::HttpController<EventsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(EventsController::active, "/v1/events", drogon::Get, "web::ApiKeyFilter");
    METHOD_LIST_END

    void active(const drogon::HttpRequestPtr& req,
                std::function<void(const drogon::HttpResponsePtr&)>&& cb);
};

}  // namespace web
