// =============================================================================
//  baas/app_setup.cc  —  see app_setup.h
// =============================================================================
#include "baas/app_setup.h"

#include <chrono>
#include <functional>
#include <string>

#include <drogon/drogon.h>
#include <json/json.h>

#include "baas/app_config.h"
#include "baas/common/context_keys.h"
#include "baas/common/errors.h"
#include "baas/gateway/rate_limiter.h"

namespace web {

void register_routes() {
    // ---- gateway rate limiting (pre-routing, so an over-limit caller is rejected
    // before any DB work) — only when configured, and only on /v1/* API routes so
    // static/dashboard/WASM asset loads are never throttled. Keyed by api-key, or
    // by client IP when the request has none.
    if (config().rate_capacity > 0) {
        static RateLimiter limiter(config().rate_capacity, config().rate_refill_per_sec);
        static const auto  t0 = std::chrono::steady_clock::now();
        drogon::app().registerPreRoutingAdvice(
            [](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& stop,
               drogon::AdviceChainCallback&& pass) {
                if (req->path().rfind("/v1/", 0) != 0) { pass(); return; }   // API routes only
                std::string key = req->getHeader("x-api-key");
                if (key.empty()) key = "ip:" + req->getPeerAddr().toIp();
                const double now =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                if (limiter.allow(key, now)) pass();
                else stop(make_error(429, "rate_limited", "too many requests"));
            });
    }

    // Liveness probe — no auth, dependency-free; used by tests + orchestration.
    drogon::app().registerHandler(
        "/healthz",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            Json::Value j;
            j["status"] = "ok";
            cb(drogon::HttpResponse::newHttpJsonResponse(j));
        },
        {drogon::Get});

    // Authenticated liveness — behind the api-key gate; echoes the resolved
    // project so we can prove the ApiKeyFilter runs and attaches kProjectId.
    drogon::app().registerHandler(
        "/v1/ping",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            Json::Value j;
            j["project_id"] =
                static_cast<Json::Int64>(req->attributes()->get<long>(kProjectId));
            cb(drogon::HttpResponse::newHttpJsonResponse(j));
        },
        {drogon::Get, std::string("web::ApiKeyFilter")});
}

}  // namespace web
