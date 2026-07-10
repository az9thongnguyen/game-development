// =============================================================================
//  baas/live_events/events_controller.cc  —  see events_controller.h
// =============================================================================
#include "baas/live_events/events_controller.h"

#include <exception>

#include <json/json.h>

#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/live_events/events_service.h"

namespace web {

void EventsController::active(const drogon::HttpRequestPtr& req,
                            std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
    const long pid = req->attributes()->get<long>(kProjectId);
    try {
        Json::Value arr(Json::arrayValue);
        for (const auto& e : live::active(pid)) {
            Json::Value j;
            j["key"]     = e.key;
            j["name"]    = e.name;
            j["payload"] = e.payload;
            arr.append(j);
        }
        Json::Value out;
        out["events"] = arr;
        cb(drogon::HttpResponse::newHttpJsonResponse(out));
    } catch (const std::exception&) {
        cb(make_error(500, "internal", "events read failed"));
    }
}

}  // namespace web
