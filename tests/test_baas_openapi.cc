// =============================================================================
//  tests/test_baas_openapi.cc  —  the description, and the two ways it can drift
// =============================================================================
//  Chapter 142. `baas/openapi/spec.cc` is a table of every operation this server
//  answers to, and `openapi::document` turns it into an OpenAPI 3.0 file. A hand-
//  written spec beside a hand-written router is two tables that agree on the day
//  they are written and never again, so this file closes both directions:
//
//    1. against the ROUTER — the set of (METHOD, path) Drogon actually registered
//       must equal the set the table documents. Adding a route without a row here
//       is red; deleting a route and leaving its row is red.
//    2. against the FILE — `baas/openapi.json` is committed, and re-baking it must
//       produce the same bytes. That is the standard `.recipe`, `collection.json`
//       and `reference.crep` are already held to: a change to the API arrives as a
//       diff somebody reads.
//
//  There are NO exemptions. The one route that needed thought — the WebSocket, which
//  Drogon lists once per HTTP method because an upgrade is not really any of them — is
//  handled by a RULE inside `live_routes()` that reads Drogon's own description, not by
//  a hard-coded name here.
// =============================================================================
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <json/json.h>

#include "baas/app_config.h"
#include "baas/app_setup.h"
#include "baas/openapi/openapi.h"
#include "tests/baas_test_util.h"

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

int main() {
    // ---- 1. the document is well-formed and says what the table says ---------
    const std::string json =
        web::openapi::document(web::openapi::spec(), web::openapi::kApiVersion);
    const Json::Value doc = baastest::parse(json);

    CHECK(doc["openapi"].asString() == "3.0.3");
    CHECK(doc["info"]["version"].asString() == std::string(web::openapi::kApiVersion));
    CHECK(doc["components"]["securitySchemes"].isMember("apiKey"));
    CHECK(doc["components"]["securitySchemes"].isMember("bearer"));
    CHECK(doc["components"]["securitySchemes"].isMember("secretKey"));
    CHECK(doc["components"]["securitySchemes"].isMember("adminSecret"));

    // Every operation in the table is in the document, under its own method, with a
    // summary, a security block and at least one response.
    int ops = 0;
    for (const auto& op : web::openapi::spec()) {
        std::string method = op.method;
        for (char& c : method) c = static_cast<char>(std::tolower((unsigned char)c));
        const Json::Value& entry = doc["paths"][op.path][method];
        if (entry.isNull()) {
            std::printf("FAIL missing from the document: %s %s\n", op.method, op.path);
            ++g_failures;
            continue;
        }
        ++ops;
        CHECK(!entry["summary"].asString().empty());
        CHECK(entry["responses"].size() > 0);
        CHECK(entry.isMember("security"));
        CHECK(!entry["operationId"].asString().empty());
    }
    CHECK(ops == static_cast<int>(web::openapi::spec().size()));

    // Every operationId is UNIQUE. OpenAPI requires it, and it is the property that
    // fails first if the id stops being derived from BOTH the method and the path —
    // `/v1/saves/{slot}` alone carries three operations.
    std::vector<std::string> ids;
    for (const auto& path : doc["paths"].getMemberNames())
        for (const auto& method : doc["paths"][path].getMemberNames())
            ids.push_back(doc["paths"][path][method]["operationId"].asString());
    std::sort(ids.begin(), ids.end());
    const auto dup = std::adjacent_find(ids.begin(), ids.end());
    if (dup != ids.end()) {
        std::printf("FAIL duplicate operationId: %s\n", dup->c_str());
        ++g_failures;
    }
    CHECK(ids.size() == web::openapi::spec().size());
    std::printf("  %d operations, %u paths\n", ops, doc["paths"].size());

    // A path parameter is DERIVED from the path, never listed twice.
    const Json::Value& put_save = doc["paths"]["/v1/saves/{slot}"]["put"];
    CHECK(put_save["parameters"].size() == 1);
    CHECK(put_save["parameters"][0]["name"].asString() == "slot");
    CHECK(put_save["parameters"][0]["in"].asString() == "path");
    CHECK(put_save["security"][0].isMember("apiKey"));
    CHECK(put_save["security"][0].isMember("bearer"));
    // ...and a route with no parameters has no parameters block at all.
    CHECK(!doc["paths"]["/v1/saves"]["get"].isMember("parameters"));
    // /healthz asks for nothing.
    CHECK(doc["paths"]["/healthz"]["get"]["security"].empty());

    // Deterministic: the same table produces the same bytes, every time.
    CHECK(web::openapi::document(web::openapi::spec(), web::openapi::kApiVersion) == json);

    // ---- 2. the committed file is what this code generates -------------------
    // Re-bake and compare BYTES, like a .recipe. `--openapi <file>` writes exactly
    // this, plus the trailing newline a text file ends with.
    {
        const std::string  path = std::string(REPO_ROOT) + "/" + web::openapi::kSpecPath;
        std::ifstream      in(path, std::ios::binary);
        std::ostringstream buf;
        buf << in.rdbuf();
        const std::string on_disk = buf.str();
        if (on_disk.empty()) {
            std::printf("FAIL %s is missing or empty\n", path.c_str());
            ++g_failures;
        } else {
            CHECK(on_disk == json + "\n");
            if (on_disk != json + "\n")
                std::printf("      re-bake it:  ./build/baas/baas --openapi %s\n",
                            web::openapi::kSpecPath);
        }
    }

    // ---- 3. the router and the table describe the same server ----------------
    // The app is SET UP but never run: registration is what is being inspected, and
    // a listener would only add a port to fight over.
    web::AppConfig cfg;
    cfg.jwt_secret   = "t";
    cfg.admin_secret = "t";
    web::set_config(cfg);
    web::register_routes();

    std::vector<std::string> live = web::openapi::live_routes();
    const std::vector<std::string> want = web::openapi::documented_routes();

    for (const auto& r : want)
        if (std::find(live.begin(), live.end(), r) == live.end()) {
            std::printf("FAIL documented but not registered: %s\n", r.c_str());
            ++g_failures;
        }
    for (const auto& r : live)
        if (std::find(want.begin(), want.end(), r) == want.end()) {
            std::printf("FAIL registered but not documented: %s\n", r.c_str());
            ++g_failures;
        }
    std::printf("  %zu routes registered, %zu documented\n", live.size(), want.size());
    // A floor, so an empty handler list cannot pass as agreement with an empty table.
    CHECK(live.size() >= 45);

    if (g_failures == 0) std::printf("baas_openapi: all tests passed\n");
    else                 std::printf("baas_openapi: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
