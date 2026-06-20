// =============================================================================
//  server/main.cpp  —  routing + entry point for the native webserver
// =============================================================================
//  A SEPARATE process (requirements.md §11): it serves the WebAssembly bundle and
//  a tiny leaderboard API over HTTP, and links NONE of the engine/game code. The
//  game and this server meet only over HTTP — never by linking.
//
//  Usage:
//      webserver [--root DIR] [--port N] [--host IP] [--scores FILE]
//  Defaults: --root build-web  --port 8080  --host 127.0.0.1  --scores scores.json
// =============================================================================
#include <cstdio>
#include <cstdlib>
#include <string>

#include "server/http.hpp"
#include "server/leaderboard.hpp"
#include "server/mime.hpp"
#include "server/net.hpp"
#include "server/static_files.hpp"

int main(int argc, char** argv) {
    std::string host        = "127.0.0.1";   // local only by default (not LAN-exposed)
    int         port        = 8080;
    std::string root        = "build-web";
    std::string scores_path = "scores.json";

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const std::string& def) {
            return (i + 1 < argc) ? std::string(argv[++i]) : def;
        };
        if      (a == "--port")   port        = std::atoi(next("8080").c_str());
        else if (a == "--root")   root        = next("build-web");
        else if (a == "--host")   host        = next("127.0.0.1");
        else if (a == "--scores") scores_path = next("scores.json");
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }

    web::Leaderboard board;
    board.load(scores_path);   // best-effort; absent file is fine

    const auto handler = [&](const web::Request& req) -> web::Response {
        const std::string path = req.target.substr(0, req.target.find('?'));

        // ---- leaderboard API (the game↔service boundary) ----
        if (path == "/api/scores") {
            if (req.method == "GET")
                return web::make_response(200, "OK", board.to_json(), "application/json");
            if (req.method == "POST") {
                std::string name;
                long        value = 0;
                if (!web::parse_score_body(req.body, name, value))
                    return web::make_response(400, "Bad Request",
                                              "{\"error\":\"expected {\\\"name\\\":..,\\\"score\\\":..}\"}\n",
                                              "application/json");
                board.add(name, value);
                board.save(scores_path);
                return web::make_response(200, "OK", board.to_json(), "application/json");
            }
            return web::make_response(405, "Method Not Allowed", "method not allowed\n");
        }

        // ---- static files (the WASM bundle) ----
        if (req.method != "GET" && req.method != "HEAD")
            return web::make_response(405, "Method Not Allowed", "method not allowed\n");

        const auto file = web::resolve(root, req.target);
        if (!file) return web::make_response(403, "Forbidden", "forbidden\n");
        auto bytes = web::read_file(*file);
        if (!bytes) return web::make_response(404, "Not Found", "not found\n");

        web::Response r;
        r.content_type = web::mime_for(*file);
        r.body         = std::move(*bytes);
        return r;
    };

    return web::serve(host, port, handler);
}
