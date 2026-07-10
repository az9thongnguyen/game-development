// =============================================================================
//  sdk/cpp/src/transport_curl.cc  —  native HTTP transport (libcurl multi)
// =============================================================================
//  Non-blocking via the curl "multi" interface: send() queues an easy handle and
//  returns immediately; poll() advances all transfers and fires the callback of
//  any that finished. This is the desktop half of the transport seam.
// =============================================================================
#include <cstddef>
#include <memory>
#include <string>

#include <curl/curl.h>

#include "gbaas/transport.h"

namespace gbaas {
namespace {

constexpr std::size_t kMaxBodyBytes = 8 * 1024 * 1024;   // cap response size (memory-DoS guard)

std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto*             body = static_cast<std::string*>(userdata);
    const std::size_t n    = size * nmemb;
    if (body->size() + n > kMaxBodyBytes) return 0;   // returning < n aborts the transfer
    body->append(ptr, n);
    return n;
}

// One in-flight request; heap-owned so its addresses stay stable for libcurl.
struct Pending {
    CURL*              easy    = nullptr;
    curl_slist*        headers = nullptr;
    std::string        reqbody;   // kept alive for CURLOPT_POSTFIELDS
    std::string        respbody;
    HttpDone           done;
};

class HttpTransportCurl : public ITransport {
public:
    HttpTransportCurl() {
        curl_global_init(CURL_GLOBAL_DEFAULT);   // refcounted by libcurl; balanced in dtor
        multi_ = curl_multi_init();
    }
    ~HttpTransportCurl() override {
        if (multi_) curl_multi_cleanup(multi_);
        curl_global_cleanup();
    }

    void send(const std::string& method, const std::string& url, const Headers& headers,
              const std::string& body, HttpDone done) override {
        auto* p     = new Pending();
        p->easy     = curl_easy_init();
        p->reqbody  = body;
        p->done     = std::move(done);

        if (!p->easy || !multi_) {   // OOM / init failure → report a transport error, don't crash
            HttpDone d = std::move(p->done);
            if (p->easy) curl_easy_cleanup(p->easy);
            delete p;
            if (d) d(HttpResponse{-1, ""});
            return;
        }

        curl_easy_setopt(p->easy, CURLOPT_URL, url.c_str());
        curl_easy_setopt(p->easy, CURLOPT_CUSTOMREQUEST, method.c_str());
        for (const auto& [k, v] : headers)
            p->headers = curl_slist_append(p->headers, (k + ": " + v).c_str());
        if (!body.empty()) {
            p->headers = curl_slist_append(p->headers, "Content-Type: application/json");
            curl_easy_setopt(p->easy, CURLOPT_POSTFIELDS, p->reqbody.c_str());
            curl_easy_setopt(p->easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(p->reqbody.size()));
        }
        if (p->headers) curl_easy_setopt(p->easy, CURLOPT_HTTPHEADER, p->headers);
        curl_easy_setopt(p->easy, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(p->easy, CURLOPT_WRITEDATA, &p->respbody);
        curl_easy_setopt(p->easy, CURLOPT_PRIVATE, p);
        curl_easy_setopt(p->easy, CURLOPT_TIMEOUT, 15L);
        curl_multi_add_handle(multi_, p->easy);
    }

    void poll() override {
        int running = 0;
        curl_multi_perform(multi_, &running);

        CURLMsg* msg = nullptr;
        int      left = 0;
        while ((msg = curl_multi_info_read(multi_, &left)) != nullptr) {
            if (msg->msg != CURLMSG_DONE) continue;
            CURL*    easy = msg->easy_handle;
            Pending* p    = nullptr;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &p);

            HttpResponse r;
            if (msg->data.result != CURLE_OK) {
                r.status = -1;
            } else {
                long code = 0;
                curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
                r.status = static_cast<int>(code);
            }
            r.body = std::move(p->respbody);

            // Detach + free BEFORE invoking the callback, so the callback may safely
            // enqueue new requests.
            HttpDone done = std::move(p->done);
            curl_multi_remove_handle(multi_, easy);
            curl_easy_cleanup(easy);
            if (p->headers) curl_slist_free_all(p->headers);
            delete p;
            if (done) done(std::move(r));
        }
    }

private:
    CURLM* multi_ = nullptr;
};

}  // namespace

std::unique_ptr<ITransport> make_default_transport() {
    return std::make_unique<HttpTransportCurl>();
}

}  // namespace gbaas
