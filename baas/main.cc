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
//  Usage:  baas [--host IP] [--port N] [--db URL] [--seed]
//    --db    sqlite://PATH (default sqlite://baas.db) or postgres://... (needs libpq)
//    --seed  create the demo project + colony_high leaderboard, print the key, exit
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <string>

#include <drogon/drogon.h>

#include "baas/db/db.h"

int main(int argc, char** argv) {
    std::string host   = "127.0.0.1";   // local only by default (not LAN-exposed)
    int         port   = 8080;
    std::string db_url = "sqlite://baas.db";
    bool        do_seed = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* def) {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string(def);
        };
        if      (a == "--host") host   = next("127.0.0.1");
        else if (a == "--port") port   = std::atoi(next("8080").c_str());
        else if (a == "--db")   db_url = next("sqlite://baas.db");
        else if (a == "--seed") do_seed = true;
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }

    // ---- database: connect + migrate (+ optional seed) ----
    try {
        auto db = web::db::make_db_client(db_url);
        web::db::set_client(db);
        web::db::run_migrations(db);
        if (do_seed) {
            const std::string pk = web::db::seed(db);
            std::printf("seeded. project public_key = %s\n", pk.c_str());
            return 0;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "db init failed: %s\n", e.what());
        return 1;
    }

    // Liveness probe: cheap, dependency-free, used by tests and orchestration.
    drogon::app().registerHandler(
        "/healthz",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value j;
            j["status"] = "ok";
            callback(drogon::HttpResponse::newHttpJsonResponse(j));
        },
        {drogon::Get});

    LOG_INFO << "baas listening on " << host << ":" << port;
    drogon::app().addListener(host, port).run();
    return 0;
}
