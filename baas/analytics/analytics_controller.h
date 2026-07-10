// =============================================================================
//  baas/analytics/analytics_controller.h  —  HTTP edge for /v1/analytics
// =============================================================================
//  Ingest only, api-key gated (events may be anonymous — before login). If a valid
//  Bearer token is present the event is attributed to that user (best-effort).
// =============================================================================
#pragma once

#include <functional>

#include <drogon/HttpController.h>

namespace web {

class AnalyticsController : public drogon::HttpController<AnalyticsController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(AnalyticsController::track, "/v1/analytics/events", drogon::Post, "web::ApiKeyFilter");
    METHOD_LIST_END

    void track(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& cb);
};

}  // namespace web
