// =============================================================================
//  sdk/cpp/src/client.cc  —  see gbaas/client.h
// =============================================================================
#include "gbaas/client.h"

#include <string>
#include <utility>

#include "gbaas/realtime.h"   // complete type for the unique_ptr<Realtime> member/dtor

namespace gbaas {

// Provided by the platform transport TU (transport_curl.cc / transport_emscripten.cc),
// selected by CMake. Lets Client(Config) pick the right default transport.
std::unique_ptr<ITransport> make_default_transport();

Client::Client(Config cfg) : cfg_(std::move(cfg)), transport_(make_default_transport()) {}
Client::Client(Config cfg, std::unique_ptr<ITransport> transport)
    : cfg_(std::move(cfg)), transport_(std::move(transport)) {}
Client::~Client() = default;

void Client::update() {
    transport_->poll();
    if (rt_) rt_->update();   // pump the realtime channel too, if one was opened
}

Session Client::parse_session_and_store(const json::Value& j) {
    token_ = j["access_token"].as_string();
    const auto& u = j["user"];
    return Session{u["user_id"].as_int(), u["display_name"].as_string(), u["is_guest"].as_bool()};
}

template <class T>
void Client::request(const std::string& method, const std::string& path,
                     const std::string& body,
                     std::function<T(const json::Value&)> extract,
                     std::function<void(Result<T>)> cb) {
    Headers headers{{"X-Api-Key", cfg_.api_key}};
    if (!token_.empty()) headers.push_back({"Authorization", "Bearer " + token_});

    transport_->send(
        method, cfg_.base_url + path, headers, body,
        [extract = std::move(extract), cb = std::move(cb)](HttpResponse resp) {
            if (resp.status < 0) {
                cb(Result<T>::err({"transport", "request failed", -1}));
                return;
            }
            const auto parsed = json::parse(resp.body);
            if (resp.status >= 200 && resp.status < 300) {
                if (!parsed) {
                    cb(Result<T>::err({"bad_response", "invalid JSON from server", resp.status}));
                    return;
                }
                cb(Result<T>::ok(extract(*parsed)));
            } else {
                Error e{"error", "request failed", resp.status};
                if (parsed && parsed->has("error")) {
                    const auto& er = (*parsed)["error"];
                    e.code    = er["code"].as_string(e.code);
                    e.message = er["message"].as_string(e.message);
                }
                cb(Result<T>::err(std::move(e)));
            }
        });
}

// -------- Auth --------
void Client::Auth::guest(std::function<void(Result<Session>)> cb) {
    c_->request<Session>("POST", "/v1/auth/guest", "{}",
                         [c = c_](const json::Value& j) { return c->parse_session_and_store(j); },
                         std::move(cb));
}

void Client::Auth::login(const std::string& email, const std::string& password,
                         std::function<void(Result<Session>)> cb) {
    const std::string body = "{\"email\":\"" + json::escape(email) + "\",\"password\":\"" +
                             json::escape(password) + "\"}";
    c_->request<Session>("POST", "/v1/auth/login", body,
                         [c = c_](const json::Value& j) { return c->parse_session_and_store(j); },
                         std::move(cb));
}

void Client::Auth::registerUser(const std::string& email, const std::string& password,
                                const std::string& display_name,
                                std::function<void(Result<Session>)> cb) {
    const std::string body = "{\"email\":\"" + json::escape(email) + "\",\"password\":\"" +
                             json::escape(password) + "\",\"display_name\":\"" +
                             json::escape(display_name) + "\"}";
    c_->request<Session>("POST", "/v1/auth/register", body,
                         [c = c_](const json::Value& j) { return c->parse_session_and_store(j); },
                         std::move(cb));
}

// -------- Leaderboard --------
void Client::Leaderboard::submit(long long value, std::function<void(Result<Rank>)> cb) {
    const std::string body = "{\"value\":" + std::to_string(value) + "}";
    c_->request<Rank>("POST", "/v1/leaderboards/" + key_ + "/scores", body,
                      [](const json::Value& j) {
                          return Rank{j["value"].as_int(), static_cast<int>(j["rank"].as_int()),
                                      j["updated"].as_bool()};
                      },
                      std::move(cb));
}

void Client::Leaderboard::top(int limit, std::function<void(Result<Board>)> cb) {
    c_->request<Board>("GET", "/v1/leaderboards/" + key_ + "/top?limit=" + std::to_string(limit), "",
                       [](const json::Value& j) {
                           Board       b;
                           const auto& entries = j["entries"];
                           for (std::size_t k = 0; k < entries.size(); ++k) {
                               const auto& e = entries[k];
                               b.entries.push_back({static_cast<int>(e["rank"].as_int()),
                                                    e["user_id"].as_int(),
                                                    e["display_name"].as_string(),
                                                    e["value"].as_int()});
                           }
                           return b;
                       },
                       std::move(cb));
}

void Client::Leaderboard::me(std::function<void(Result<Rank>)> cb) {
    c_->request<Rank>("GET", "/v1/leaderboards/" + key_ + "/me", "",
                      [](const json::Value& j) {
                          return Rank{j["value"].as_int(), static_cast<int>(j["rank"].as_int()), false};
                      },
                      std::move(cb));
}

// -------- Saves (cloud save) --------
void Client::Saves::put(const std::string& slot, const std::string& data,
                        std::function<void(Result<SaveMeta>)> cb) {
    const std::string body = "{\"data\":\"" + json::escape(data) + "\"}";
    c_->request<SaveMeta>("PUT", "/v1/saves/" + slot, body,
                          [](const json::Value& j) {
                              return SaveMeta{j["slot"].as_string(), j["version"].as_int(),
                                              j["size"].as_int()};
                          },
                          std::move(cb));
}

void Client::Saves::get(const std::string& slot, std::function<void(Result<Save>)> cb) {
    c_->request<Save>("GET", "/v1/saves/" + slot, "",
                      [](const json::Value& j) {
                          return Save{j["slot"].as_string(), j["version"].as_int(),
                                      j["data"].as_string()};
                      },
                      std::move(cb));
}

void Client::Saves::list(std::function<void(Result<std::vector<SaveMeta>>)> cb) {
    c_->request<std::vector<SaveMeta>>(
        "GET", "/v1/saves", "",
        [](const json::Value& j) {
            std::vector<SaveMeta> out;
            const auto&           arr = j["saves"];
            for (std::size_t k = 0; k < arr.size(); ++k)
                out.push_back({arr[k]["slot"].as_string(), arr[k]["version"].as_int(),
                               arr[k]["size"].as_int()});
            return out;
        },
        std::move(cb));
}

void Client::Saves::remove(const std::string& slot, std::function<void(Result<bool>)> cb) {
    c_->request<bool>("DELETE", "/v1/saves/" + slot, "",
                      [](const json::Value& j) { return j["deleted"].as_bool(); },
                      std::move(cb));
}

// -------- Inventory --------
namespace {
Item extract_item(const json::Value& j) { return Item{j["item"].as_string(), j["qty"].as_int()}; }
}

void Client::Inventory::grant(const std::string& item, long long amount,
                              std::function<void(Result<Item>)> cb) {
    c_->request<Item>("POST", "/v1/inventory/" + item + "/grant",
                      "{\"amount\":" + std::to_string(amount) + "}", extract_item, std::move(cb));
}

void Client::Inventory::consume(const std::string& item, long long amount,
                                std::function<void(Result<Item>)> cb) {
    c_->request<Item>("POST", "/v1/inventory/" + item + "/consume",
                      "{\"amount\":" + std::to_string(amount) + "}", extract_item, std::move(cb));
}

void Client::Inventory::get(const std::string& item, std::function<void(Result<Item>)> cb) {
    c_->request<Item>("GET", "/v1/inventory/" + item, "", extract_item, std::move(cb));
}

void Client::Inventory::list(std::function<void(Result<std::vector<Item>>)> cb) {
    c_->request<std::vector<Item>>(
        "GET", "/v1/inventory", "",
        [](const json::Value& j) {
            std::vector<Item> out;
            const auto&       arr = j["items"];
            for (std::size_t k = 0; k < arr.size(); ++k)
                out.push_back({arr[k]["item"].as_string(), arr[k]["qty"].as_int()});
            return out;
        },
        std::move(cb));
}

// -------- Remote config --------
void Client::RemoteConfig::all(std::function<void(Result<std::vector<ConfigEntry>>)> cb) {
    c_->request<std::vector<ConfigEntry>>(
        "GET", "/v1/config", "",
        [](const json::Value& j) {
            std::vector<ConfigEntry> out;
            for (const auto& kv : j["config"].obj)   // config is a JSON object
                out.push_back({kv.first, kv.second.as_string()});
            return out;
        },
        std::move(cb));
}

void Client::RemoteConfig::get(const std::string& key, std::function<void(Result<std::string>)> cb) {
    c_->request<std::string>("GET", "/v1/config/" + key, "",
                             [](const json::Value& j) { return j["value"].as_string(); },
                             std::move(cb));
}

// -------- Analytics --------
void Client::Analytics::track(const std::string& name, const std::string& props,
                              std::function<void(Result<bool>)> cb) {
    const std::string body = "{\"name\":\"" + json::escape(name) + "\",\"props\":" + props + "}";
    c_->request<bool>("POST", "/v1/analytics/events", body,
                      [](const json::Value& j) { return j["ok"].as_bool(); },
                      cb ? std::move(cb) : [](Result<bool>) {});
}

// -------- Live events --------
void Client::LiveEvents::active(std::function<void(Result<std::vector<LiveEvent>>)> cb) {
    c_->request<std::vector<LiveEvent>>(
        "GET", "/v1/events", "",
        [](const json::Value& j) {
            std::vector<LiveEvent> out;
            const auto&            arr = j["events"];
            for (std::size_t k = 0; k < arr.size(); ++k)
                out.push_back({arr[k]["key"].as_string(), arr[k]["name"].as_string(),
                               arr[k]["payload"].as_string()});
            return out;
        },
        std::move(cb));
}

}  // namespace gbaas
