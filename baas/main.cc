// =============================================================================
//  baas/main.cc  —  Game BaaS backend entry point (Slice #1)
// =============================================================================
//  A SEPARATE process (requirements.md §11): it exposes the game↔service API over
//  HTTP and links NONE of the engine/game code. Drogon owns the event loop,
//  connection handling, and routing; our modules (gateway/auth/leaderboard) plug
//  in as filters + controllers. The engine core never sees Drogon.
//
//  Config, gateway, auth, and leaderboard land in the following sub-milestones.
//
//  Usage:  baas [--host IP] [--port N] [--db URL] [--seed | --seed-only]
//    --db         sqlite://PATH (default sqlite://baas.db) or postgres://... (libpq)
//    --seed       make sure the demo project exists, print its key, THEN SERVE. This is
//                 what a container wants on a fresh volume, and until chapter 142 it
//                 exited instead — so the shipped image seeded and stopped, every time,
//                 and had never answered a request.
//    --seed-only  ...and stop, which is what a person at a terminal wants.
//    --openapi F  write the OpenAPI document to F and exit (re-bakes baas/openapi.json)
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <string>

#include <drogon/drogon.h>
#include <sodium.h>

#include "baas/app_config.h"
#include "baas/app_setup.h"
#include "baas/db/db.h"
#include "baas/openapi/openapi.h"

int main(int argc, char** argv) {
    std::string host   = "127.0.0.1";   // local only by default (not LAN-exposed)
    int         port   = 8080;
    std::string db_url = "sqlite://baas.db";
    bool        do_seed = false;
    bool        seed_only = false;
    std::string openapi_out;   // --openapi FILE: write the spec and exit
    std::string jwt_secret;   // else BAAS_JWT_SECRET env, else an insecure dev default
    std::string admin_secret; // else BAAS_ADMIN_SECRET env, else an insecure dev default
    std::string static_root;  // optional: serve the WASM bundle from here (same-origin → no CORS)
    std::string dashboard = "baas/web/dashboard.html";   // --dashboard: the admin UI file

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* def) {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string(def);
        };
        if      (a == "--host")         host         = next("127.0.0.1");
        else if (a == "--port")         port         = std::atoi(next("8080").c_str());
        else if (a == "--db")           db_url       = next("sqlite://baas.db");
        else if (a == "--jwt-secret")   jwt_secret   = next("");
        else if (a == "--admin-secret") admin_secret = next("");
        else if (a == "--static")       static_root  = next("");
        else if (a == "--dashboard")    dashboard    = next("baas/web/dashboard.html");
        else if (a == "--seed")         do_seed      = true;
        else if (a == "--seed-only")  { do_seed      = true; seed_only = true; }
        else if (a == "--openapi")      openapi_out  = next("");
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }

    // ---- crypto + config ----
    if (sodium_init() < 0) {
        std::fprintf(stderr, "libsodium init failed\n");
        return 1;
    }
    web::AppConfig cfg;
    const char*    env_secret = std::getenv("BAAS_JWT_SECRET");
    cfg.jwt_secret = !jwt_secret.empty()
                         ? jwt_secret
                         : (env_secret ? std::string(env_secret) : std::string());
    if (cfg.jwt_secret.empty()) {
        std::fprintf(stderr,
                     "WARNING: no --jwt-secret / BAAS_JWT_SECRET set; "
                     "using an INSECURE dev secret\n");
        cfg.jwt_secret = "dev-insecure-secret-change-me";
    }
    const char* env_admin = std::getenv("BAAS_ADMIN_SECRET");
    cfg.admin_secret      = !admin_secret.empty()
                                ? admin_secret
                                : (env_admin ? std::string(env_admin) : std::string());
    if (cfg.admin_secret.empty()) {
        std::fprintf(stderr,
                     "WARNING: no --admin-secret / BAAS_ADMIN_SECRET set; "
                     "using an INSECURE dev admin secret\n");
        cfg.admin_secret = "dev-insecure-admin-change-me";
    }
    // Rate limiting: on by default (a per-api-key/IP token bucket on /v1/*).
    // Override the burst/refill or disable it (BAAS_RATE_CAPACITY=0) via env.
    cfg.rate_capacity       = 120;   // burst size
    cfg.rate_refill_per_sec = 60;    // sustained requests/sec per caller
    if (const char* c = std::getenv("BAAS_RATE_CAPACITY")) cfg.rate_capacity = std::atof(c);
    if (const char* r = std::getenv("BAAS_RATE_REFILL"))   cfg.rate_refill_per_sec = std::atof(r);
    cfg.dashboard_path = dashboard;
    web::set_config(cfg);

    // ---- database: connect + migrate (+ optional seed) ----
    try {
        auto db = web::db::make_db_client(db_url);
        web::db::set_client(db);
        web::db::run_migrations(db);
        if (do_seed) {
            const std::string pk = web::db::seed(db);
            std::printf("seeded. project public_key = %s   secret_key = sk_demo_colony\n",
                        pk.c_str());
            // ...and then KEEP GOING, unless the caller asked for the one-shot. The
            // Dockerfile's CMD has carried `--seed` since chapter 107 and this line used
            // to be `return 0`, so the container seeded and exited — with
            // `restart: unless-stopped` in front of it, forever. It had never served a
            // request; `/healthz` was a route nothing outside a test had ever reached.
            if (seed_only) return 0;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "db init failed: %s\n", e.what());
        return 1;
    }

    // --openapi: the document, written and gone. No listener, no database needed —
    // `openapi::document` is pure, which is what lets a test re-bake the committed
    // bytes and compare them.
    if (!openapi_out.empty()) {
        const std::string doc =
            web::openapi::document(web::openapi::spec(), web::openapi::kApiVersion);
        FILE* f = std::fopen(openapi_out.c_str(), "wb");
        if (!f) { std::fprintf(stderr, "cannot write %s\n", openapi_out.c_str()); return 1; }
        std::fwrite(doc.data(), 1, doc.size(), f);
        std::fputc('\n', f);
        std::fclose(f);
        std::printf("wrote %s (%zu bytes, %zu operations)\n", openapi_out.c_str(),
                    doc.size() + 1, web::openapi::spec().size());
        return 0;
    }

    web::register_routes();

    // Optionally serve the WASM bundle so the page and the API share one origin
    // (no CORS needed). Our /v1 and /healthz handlers take precedence; anything
    // else falls back to a static file under static_root.
    if (!static_root.empty()) {
        drogon::app().setDocumentRoot(static_root);
        // Drogon serves only an allowlist of extensions; add the WASM bundle's,
        // and register the MIME types browsers require (application/wasm enables
        // streaming compilation).
        drogon::app().setFileTypes({"html", "js", "wasm", "data", "css", "png", "ico", "json", "txt"});
        drogon::app().registerCustomExtensionMime("wasm", "application/wasm");
        drogon::app().registerCustomExtensionMime("data", "application/octet-stream");
        LOG_INFO << "serving static files from " << static_root;
    }

    LOG_INFO << "baas listening on " << host << ":" << port;
    drogon::app().addListener(host, port).run();
    return 0;
}
