// =============================================================================
//  baas/main.cc  —  Game BaaS backend entry point (Slice #1)
// =============================================================================
//  A SEPARATE process (requirements.md §11): it exposes the game↔service API over
//  HTTP and links NONE of the engine/game code. Drogon owns the event loop,
//  connection handling, and routing; our modules (gateway/auth/leaderboard) plug
//  in as filters + controllers. The engine core never sees Drogon.
//
//  S1.0 is intentionally the smallest thing that proves Drogon builds and boots:
//  a single `GET /healthz`. Config, DB, gateway, auth, and leaderboard land in the
//  following sub-milestones.
//
//  Usage (S1.0):  baas [--host 127.0.0.1] [--port 8080]
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include <drogon/drogon.h>

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";   // local only by default (not LAN-exposed)
    int         port = 8080;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* def) {
            return (i + 1 < argc) ? std::string(argv[++i]) : std::string(def);
        };
        if      (a == "--host") host = next("127.0.0.1");
        else if (a == "--port") port = std::atoi(next("8080").c_str());
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
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
