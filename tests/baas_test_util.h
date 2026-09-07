// =============================================================================
//  tests/baas_test_util.h  —  shared harness for BaaS integration tests
// =============================================================================
//  A synchronous libcurl HTTP driver, an ephemeral-port finder, a temp-DB
//  cleaner, and a JSON parser. Each test provides its own main()/CHECK/g_failures
//  and boots the Drogon app; these helpers are the plumbing they share.
// =============================================================================
#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <cstdlib>

#include <curl/curl.h>
#include <drogon/orm/DbClient.h>
#include <json/json.h>

namespace baastest {

inline std::size_t write_cb(char* p, std::size_t sz, std::size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, sz * n);
    return sz * n;
}

struct Resp {
    long        status = 0;
    std::string body;
    std::string raw_headers;   // all response header lines, as received
};

// Case-insensitive lookup of a response header value (trimmed); "" if absent.
inline std::string header_value(const Resp& r, const std::string& name) {
    std::string lower_all = r.raw_headers, lower_name = name;
    for (char& c : lower_all)  c = static_cast<char>(std::tolower((unsigned char)c));
    for (char& c : lower_name) c = static_cast<char>(std::tolower((unsigned char)c));
    const std::string needle = lower_name + ":";
    const std::size_t at = lower_all.find(needle);
    if (at == std::string::npos) return "";
    std::size_t vs = at + needle.size();
    std::size_t ve = r.raw_headers.find_first_of("\r\n", vs);
    std::string v  = r.raw_headers.substr(vs, ve - vs);
    const std::size_t b = v.find_first_not_of(" \t");
    const std::size_t e = v.find_last_not_of(" \t");
    return b == std::string::npos ? "" : v.substr(b, e - b + 1);
}

// Synchronous HTTP request. Adds Content-Type: application/json when a body is
// present. status == -1 signals a transport failure.
inline Resp http(const std::string& method, const std::string& url,
                 const std::vector<std::string>& headers,
                 const std::string& body = "") {
    Resp  r;
    CURL* c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
    struct curl_slist* hs = nullptr;
    for (const auto& h : headers) hs = curl_slist_append(hs, h.c_str());
    if (!body.empty()) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
        hs = curl_slist_append(hs, "Content-Type: application/json");
    }
    if (hs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hs);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &r.raw_headers);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 5L);
    if (curl_easy_perform(c) != CURLE_OK) r.status = -1;
    else curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    if (hs) curl_slist_free_all(hs);
    curl_easy_cleanup(c);
    return r;
}

// Ask the OS for a free loopback port (tiny race window, fine for a local test).
inline int find_free_port() {
    const int   s = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family      = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port        = 0;
    if (::bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) { ::close(s); return 0; }
    socklen_t len = sizeof(a);
    ::getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
    const int port = ntohs(a.sin_port);
    ::close(s);
    return port;
}

// ---- which database the tests run against -----------------------------------
//
// Default: a SQLite file named after the test. Set BAAS_TEST_DB to a postgres://
// url and the SAME suite runs against Postgres — which is the only way the row
// locks added in chapter 140 mean anything, since SQLite's pool of one hides every
// race they exist to prevent.
//
//   BAAS_TEST_DB=postgres://baas:baas@127.0.0.1:5432/baas ctest --test-dir build/baas
//
// One shared database, wiped by `cleanup_db` at the start of each test. ctest runs
// these serially (no -j), which is what makes one database safe; running them in
// parallel against Postgres would need a schema each and does not today.
inline const char* test_db_env() { return std::getenv("BAAS_TEST_DB"); }

// `name` is the FULL sqlite filename, including its extension — the tests already
// carry one ("test_baas_auth.db") and `cleanup_db` deletes exactly that path. The
// first version of this helper appended ".db" itself, so every test wrote
// `test_baas_auth.db.db` while cleanup deleted `test_baas_auth.db`: a suite that
// passed once on a clean checkout and then failed on every run after, against a
// database nothing ever emptied.
inline std::string db_url(const std::string& name) {
    if (const char* url = test_db_env()) return url;
    return "sqlite://" + name;
}

// Start from nothing. On SQLite that is deleting the file; on Postgres it is
// dropping the schema, because there is one database for all of them.
inline void cleanup_db(const std::string& path) {
    if (const char* url = test_db_env()) {
        // LEAKED on purpose. Drogon's PgClient owns an event-loop thread, and letting
        // the last reference go inside one of its own callbacks makes the destructor
        // join that thread from itself — "Resource deadlock avoided", an abort with
        // no output, before this file printed a single line. A test helper that runs
        // once per process is exactly the place to hold a handle for the life of the
        // process instead.
        static drogon::orm::DbClientPtr* wipe = nullptr;
        try {
            if (!wipe) wipe = new drogon::orm::DbClientPtr(
                drogon::orm::DbClient::newPgClient(url, 1));
            (*wipe)->execSqlSync("DROP SCHEMA IF EXISTS public CASCADE");
            (*wipe)->execSqlSync("CREATE SCHEMA public");
        } catch (const std::exception& e) {
            std::fprintf(stderr, "cleanup_db: %s\n", e.what());
        }
        return;
    }
    for (const char* suffix : {"", "-journal", "-wal", "-shm"})
        std::remove((path + suffix).c_str());
}

inline Json::Value parse(const std::string& body) {
    Json::Value                             j;
    Json::CharReaderBuilder                 rb;
    std::string                             e;
    const std::unique_ptr<Json::CharReader> r(rb.newCharReader());
    r->parse(body.data(), body.data() + body.size(), &j, &e);
    return j;
}

}  // namespace baastest
