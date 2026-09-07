// =============================================================================
//  baas/openapi/openapi.cc  —  see openapi.h
// =============================================================================
#include "baas/openapi/openapi.h"

#include <algorithm>
#include <sstream>

#include <drogon/drogon.h>
#include <json/json.h>

namespace web::openapi {
namespace {

const char* auth_scheme(Auth a) {
    switch (a) {
        case Auth::None:         return "";
        case Auth::ApiKey:       return "apiKey";
        case Auth::ApiKeyUser:   return "apiKey+bearer";
        case Auth::ApiKeySecret: return "apiKey+secretKey";
        case Auth::AdminSecret:  return "adminSecret";
    }
    return "";
}

// The path's `{name}` segments, in order. Derived rather than listed: a parameter is
// already written down once, in the path, and a second copy is a second thing to
// forget.
std::vector<std::string> path_params(const std::string& path) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] != '{') continue;
        const std::size_t end = path.find('}', i);
        if (end == std::string::npos) break;
        out.push_back(path.substr(i + 1, end - i - 1));
        i = end;
    }
    return out;
}

// A stable, readable id: "get_v1_saves_slot". Derived from the two things that are
// already unique together, so it cannot disagree with the route it names.
std::string operation_id(const std::string& method, const std::string& path) {
    std::string id;
    for (char c : method) id += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (char c : path) {
        if (c == '/' || c == '{' || c == '}' || c == '-') {
            if (!id.empty() && id.back() != '_') id += '_';
        } else {
            id += c;
        }
    }
    while (!id.empty() && id.back() == '_') id.pop_back();
    return id;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const char* method_name(drogon::HttpMethod m) {
    switch (m) {
        case drogon::Get:     return "GET";
        case drogon::Post:    return "POST";
        case drogon::Put:     return "PUT";
        case drogon::Delete:  return "DELETE";
        case drogon::Patch:   return "PATCH";
        case drogon::Head:    return "HEAD";
        case drogon::Options: return "OPTIONS";
        default:              return "";
    }
}

}  // namespace

std::string document(const std::vector<Op>& ops, const std::string& version) {
    Json::Value doc(Json::objectValue);
    doc["openapi"] = "3.0.3";

    Json::Value info(Json::objectValue);
    info["title"]   = "Game Backend-as-a-Service";
    info["version"] = version;
    info["description"] =
        "The HTTP API a game talks to: accounts, leaderboards, cloud saves, inventory, "
        "a priced catalogue, shared assets, remote config, live events, replays and "
        "headless test runs.\n\n"
        "This document is GENERATED from the same table the server routes from, and a "
        "test compares it to the routes Drogon actually registered. It cannot describe "
        "an endpoint that does not exist, and an endpoint cannot exist without appearing "
        "here.";
    doc["info"] = info;

    Json::Value server(Json::objectValue);
    server["url"]         = "/";
    server["description"] = "the server that served this document";
    doc["servers"] = Json::Value(Json::arrayValue);
    doc["servers"].append(server);

    // ---- security schemes: the four doors -----------------------------------
    Json::Value schemes(Json::objectValue);
    auto header_scheme = [](const char* name, const char* desc) {
        Json::Value s(Json::objectValue);
        s["type"]        = "apiKey";
        s["in"]          = "header";
        s["name"]        = name;
        s["description"] = desc;
        return s;
    };
    schemes["apiKey"] = header_scheme(
        "X-Api-Key", "The project's public key. Identifies the GAME, never a player.");
    Json::Value bearer(Json::objectValue);
    bearer["type"]         = "http";
    bearer["scheme"]       = "bearer";
    bearer["bearerFormat"] = "JWT";
    bearer["description"]  = "The access token from /v1/auth/*. Identifies the PLAYER.";
    schemes["bearer"]      = bearer;
    schemes["secretKey"]   = header_scheme(
        "X-Secret-Key", "The project's secret key. Identifies an OPERATOR of this game.");
    schemes["adminSecret"] = header_scheme(
        "X-Admin-Secret", "The platform secret. Identifies whoever runs the SERVER.");
    doc["components"]                    = Json::Value(Json::objectValue);
    doc["components"]["securitySchemes"] = schemes;

    // ---- tags, in first-appearance order ------------------------------------
    Json::Value tags(Json::arrayValue);
    std::vector<std::string> seen;
    for (const auto& op : ops) {
        if (std::find(seen.begin(), seen.end(), op.tag) != seen.end()) continue;
        seen.emplace_back(op.tag);
        Json::Value t(Json::objectValue);
        t["name"] = op.tag;
        tags.append(t);
    }
    doc["tags"] = tags;

    // ---- the operations -----------------------------------------------------
    Json::Value paths(Json::objectValue);
    for (const auto& op : ops) {
        Json::Value entry(Json::objectValue);
        entry["tags"] = Json::Value(Json::arrayValue);
        entry["tags"].append(op.tag);
        entry["summary"]     = op.summary;
        entry["operationId"] = operation_id(op.method, op.path);

        // security: an array of requirement OBJECTS, each of which is an AND of the
        // schemes it names. Two headers on one route is one object with two keys.
        const std::string scheme = auth_scheme(op.auth);
        Json::Value       sec(Json::arrayValue);
        if (!scheme.empty()) {
            Json::Value need(Json::objectValue);
            std::size_t start = 0;
            while (start <= scheme.size()) {
                const std::size_t plus = scheme.find('+', start);
                const std::string one =
                    scheme.substr(start, plus == std::string::npos ? std::string::npos
                                                                   : plus - start);
                need[one] = Json::Value(Json::arrayValue);
                if (plus == std::string::npos) break;
                start = plus + 1;
            }
            sec.append(need);
        }
        entry["security"] = sec;

        Json::Value params(Json::arrayValue);
        for (const auto& name : path_params(op.path)) {
            Json::Value p(Json::objectValue);
            p["name"]     = name;
            p["in"]       = "path";
            p["required"] = true;
            p["schema"]   = Json::Value(Json::objectValue);
            p["schema"]["type"] = "string";
            params.append(p);
        }
        if (!params.empty()) entry["parameters"] = params;

        if (op.body && *op.body) {
            Json::Value body(Json::objectValue);
            body["required"]    = true;
            body["description"] = op.body;
            Json::Value content(Json::objectValue);
            content["application/json"] = Json::Value(Json::objectValue);
            content["application/json"]["schema"] = Json::Value(Json::objectValue);
            content["application/json"]["schema"]["type"] = "object";
            body["content"]      = content;
            entry["requestBody"] = body;
        }

        Json::Value replies(Json::objectValue);
        for (const auto& r : op.replies) {
            Json::Value one(Json::objectValue);
            one["description"] = r.what;
            replies[std::to_string(r.code)] = one;
        }
        entry["responses"] = replies;

        paths[op.path][lower(op.method)] = entry;
    }
    doc["paths"] = paths;

    Json::StreamWriterBuilder w;
    w["indentation"] = "  ";
    w["commentStyle"] = "None";
    return Json::writeString(w, doc);
}

std::vector<std::string> live_routes() {
    std::vector<std::string> out;
    for (const auto& h : drogon::app().getHandlersInfo()) {
        const std::string path = std::get<0>(h);
        const std::string what = std::get<2>(h);

        // A WebSocket controller is listed once per HTTP METHOD — eleven rows for one
        // route — because the upgrade is not really any of them. It is a GET that
        // becomes something else, so that is what it is reported and documented as.
        // Reading Drogon's own description is what keeps this a RULE rather than a
        // hard-coded exception for `/v1/ws`.
        if (what.rfind("WebsocketController:", 0) == 0) {
            out.push_back("GET " + path);
            continue;
        }

        const char* m = method_name(std::get<1>(h));
        if (!*m) continue;
        out.push_back(std::string(m) + " " + path);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<std::string> documented_routes() {
    std::vector<std::string> out;
    for (const auto& op : spec()) out.push_back(std::string(op.method) + " " + op.path);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace web::openapi
