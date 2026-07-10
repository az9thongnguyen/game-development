// =============================================================================
//  sdk/cpp/src/transport_emscripten.cc  —  web HTTP transport (emscripten_fetch)
// =============================================================================
//  The WASM half of the transport seam. emscripten_fetch is already async and
//  driven by the browser's event loop, so send() just kicks off a fetch and
//  poll() is a no-op — the success/error handlers deliver the response. Request
//  data (body, header strings, the callback) must outlive the async call, so they
//  live in a heap-owned context freed in the handler. Built only for EMSCRIPTEN
//  (see sdk/cpp/CMakeLists.txt), linked with -sFETCH.
// =============================================================================
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <emscripten/fetch.h>

#include "gbaas/transport.h"

namespace gbaas {
namespace {

// Owns everything the async fetch must keep alive until it completes.
struct FetchCtx {
    std::string              body;           // requestData points here
    std::vector<std::string> header_storage; // name,value,name,value,…
    std::vector<const char*> header_ptrs;    // null-terminated array for emscripten
    HttpDone                 done;
};

constexpr std::size_t kMaxBodyBytes = 8 * 1024 * 1024;   // cap response size (memory-DoS guard)

void finish(emscripten_fetch_t* fetch, int status) {
    auto* ctx = static_cast<FetchCtx*>(fetch->userData);
    HttpResponse r;
    r.status = status;
    if (fetch->data && fetch->numBytes > 0) {
        if (static_cast<std::size_t>(fetch->numBytes) > kMaxBodyBytes) r.status = -1;
        else r.body.assign(fetch->data, static_cast<std::size_t>(fetch->numBytes));
    }
    HttpDone done = std::move(ctx->done);
    delete ctx;
    emscripten_fetch_close(fetch);
    if (done) done(std::move(r));
}

void on_success(emscripten_fetch_t* fetch) { finish(fetch, static_cast<int>(fetch->status)); }
void on_error(emscripten_fetch_t* fetch) {
    finish(fetch, fetch->status ? static_cast<int>(fetch->status) : -1);
}

class HttpTransportEmscripten : public ITransport {
public:
    void send(const std::string& method, const std::string& url, const Headers& headers,
              const std::string& body, HttpDone done) override {
        auto* ctx = new FetchCtx();
        ctx->body = body;
        ctx->done = std::move(done);

        for (const auto& [k, v] : headers) {
            ctx->header_storage.push_back(k);
            ctx->header_storage.push_back(v);
        }
        if (!body.empty()) {
            ctx->header_storage.push_back("Content-Type");
            ctx->header_storage.push_back("application/json");
        }
        for (const auto& s : ctx->header_storage) ctx->header_ptrs.push_back(s.c_str());
        ctx->header_ptrs.push_back(nullptr);   // terminator

        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        std::snprintf(attr.requestMethod, sizeof(attr.requestMethod), "%s", method.c_str());
        attr.attributes    = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attr.userData      = ctx;
        attr.onsuccess     = on_success;
        attr.onerror       = on_error;
        attr.requestHeaders = ctx->header_ptrs.data();
        if (!ctx->body.empty()) {
            attr.requestData     = ctx->body.data();
            attr.requestDataSize = ctx->body.size();
        }
        emscripten_fetch(&attr, url.c_str());
    }

    void poll() override {}   // the browser event loop drives fetch; nothing to pump
};

}  // namespace

std::unique_ptr<ITransport> make_default_transport() {
    return std::make_unique<HttpTransportEmscripten>();
}

}  // namespace gbaas
