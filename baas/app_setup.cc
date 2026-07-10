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
#include "baas/observability/metrics.h"

namespace web {

void register_routes() {
    // A shared monotonic origin so the stamp + pre-sending advices measure the same
    // clock (captured by value into the advice lambdas).
    const auto t0      = std::chrono::steady_clock::now();
    auto       elapsed = [t0] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };

    // ---- (1) stamp each request's start time. Registered FIRST so it runs before
    // the rate-limit advice, and even rejected (429) requests carry a start stamp
    // for the access log.
    drogon::app().registerPreRoutingAdvice(
        [elapsed](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&&,
                  drogon::AdviceChainCallback&& pass) {
            req->attributes()->insert("t_start", elapsed());
            pass();
        });

    // ---- (2) gateway rate limiting (pre-routing, so an over-limit caller is
    // rejected before any DB work) — only when configured, and only on /v1/* API
    // routes so static/dashboard/WASM asset loads are never throttled. Keyed by
    // api-key, or by client IP when the request has none.
    if (config().rate_capacity > 0) {
        static RateLimiter limiter(config().rate_capacity, config().rate_refill_per_sec);
        drogon::app().registerPreRoutingAdvice(
            [elapsed](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& stop,
                      drogon::AdviceChainCallback&& pass) {
                if (req->path().rfind("/v1/", 0) != 0) { pass(); return; }   // API routes only
                std::string key = req->getHeader("x-api-key");
                if (key.empty()) key = "ip:" + req->getPeerAddr().toIp();
                if (limiter.allow(key, elapsed())) pass();
                else stop(make_error(429, "rate_limited", "too many requests"));
            });
    }

    // ---- (3) observability: on every response, count it and emit a structured
    // access-log line. registerPreSendingAdvice fires for EVERY response (including
    // 404s and pre-routing 429s), so nothing escapes the metrics.
    drogon::app().registerPreSendingAdvice(
        [elapsed](const drogon::HttpRequestPtr& req, const drogon::HttpResponsePtr& resp) {
            const int status = static_cast<int>(resp->getStatusCode());
            Metrics::instance().record(req->path(), status);
            const double dur_ms =
                (elapsed() - req->attributes()->get<double>("t_start")) * 1000.0;
            LOG_INFO << req->methodString() << " " << req->path() << " " << status << " "
                     << dur_ms << "ms";
        });

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

    // Metrics scrape — platform-admin only (X-Admin-Secret). Returns the request
    // totals, the status-class tally, and the per-route tally as JSON.
    drogon::app().registerHandler(
        "/metrics",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            const auto  snap = Metrics::instance().snapshot();
            Json::Value j;
            j["total"] = static_cast<Json::Int64>(snap.total);
            Json::Value by_status(Json::objectValue);
            for (const auto& kv : snap.by_status)
                by_status[kv.first] = static_cast<Json::Int64>(kv.second);
            j["by_status"] = by_status;
            Json::Value by_path(Json::objectValue);
            for (const auto& kv : snap.by_path)
                by_path[kv.first] = static_cast<Json::Int64>(kv.second);
            j["by_path"] = by_path;
            cb(drogon::HttpResponse::newHttpJsonResponse(j));
        },
        {drogon::Get, std::string("web::AdminSecretFilter")});
}

}  // namespace web
