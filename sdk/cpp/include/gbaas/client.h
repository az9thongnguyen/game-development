// =============================================================================
//  gbaas/client.h  —  the unified, non-blocking SDK client
// =============================================================================
//  Usage mirrors the engine's no-blocking-loop rule: calls return immediately and
//  deliver results via callback when Client::update() is pumped from the game tick.
//    gbaas::Client c({.base_url="http://127.0.0.1:8080", .api_key="pk_demo_colony"});
//    c.auth().guest([&](gbaas::Result<gbaas::Session> r){ ... });
//    c.leaderboard("colony_high").submit(4200, [&](auto r){ ... });
//    // each frame:
//    c.update();
//  The api key is sent on every request; after a successful auth the access token
//  is attached automatically. The Client must outlive its in-flight requests.
// =============================================================================
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "gbaas/json.hpp"
#include "gbaas/result.h"
#include "gbaas/transport.h"

namespace gbaas {

struct Config {
    std::string base_url;   // e.g. "http://127.0.0.1:8080"
    std::string api_key;    // the project's public key
};

struct Session {
    long long   user_id = 0;   // 64-bit: wasm32/Windows have 32-bit long
    std::string display_name;
    bool        is_guest = false;
};

struct Rank {
    long long value   = 0;
    int       rank    = 0;
    bool      updated = false;
};

struct Entry {
    int         rank = 0;
    long long   user_id = 0;
    std::string display_name;
    long long   value = 0;
};

struct Board {
    std::vector<Entry> entries;
};

class Client {
public:
    // Native default transport (libcurl). Provide a transport explicitly for the
    // web build or for tests (a fake).
    explicit Client(Config cfg);
    Client(Config cfg, std::unique_ptr<ITransport> transport);
    ~Client();

    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;

    // Pump completed transfers → fire callbacks. Call once per frame.
    void update();

    // The access token, empty until a successful auth (exposed for tests/inspection).
    const std::string& token() const { return token_; }

    // ---- service handles (client.<service>().<action>(args, callback)) ----
    class Auth {
    public:
        void guest(std::function<void(Result<Session>)> cb);
        void login(const std::string& email, const std::string& password,
                   std::function<void(Result<Session>)> cb);
        void registerUser(const std::string& email, const std::string& password,
                          const std::string& display_name,
                          std::function<void(Result<Session>)> cb);
    private:
        friend class Client;
        explicit Auth(Client* c) : c_(c) {}
        Client* c_;
    };

    class Leaderboard {
    public:
        void submit(long long value, std::function<void(Result<Rank>)> cb);
        void top(int limit, std::function<void(Result<Board>)> cb);
        void me(std::function<void(Result<Rank>)> cb);
    private:
        friend class Client;
        Leaderboard(Client* c, std::string key) : c_(c), key_(std::move(key)) {}
        Client*     c_;
        std::string key_;
    };

    Auth        auth() { return Auth(this); }
    Leaderboard leaderboard(std::string key) { return Leaderboard(this, std::move(key)); }

private:
    // Build headers, send, and route the response into a Result<T> via `extract`.
    template <class T>
    void request(const std::string& method, const std::string& path, const std::string& body,
                 std::function<T(const json::Value&)> extract,
                 std::function<void(Result<T>)> cb);

    // Extract a Session from an auth response AND store its access token.
    Session parse_session_and_store(const json::Value& j);

    Config                      cfg_;
    std::unique_ptr<ITransport> transport_;
    std::string                 token_;
};

}  // namespace gbaas
