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

#include <curl/curl.h>
#include <json/json.h>

namespace baastest {

inline std::size_t write_cb(char* p, std::size_t sz, std::size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, sz * n);
    return sz * n;
}

struct Resp {
    long        status = 0;
    std::string body;
};

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

inline void cleanup_db(const std::string& path) {
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
