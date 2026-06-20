// =============================================================================
//  server/net.cpp  —  POSIX socket accept loop
// =============================================================================
#include "server/net.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace web {
namespace {

constexpr std::size_t kMaxRequest = 1u << 20;   // 1 MiB cap → no unbounded-memory DoS

// Parse Content-Length from a header block (case-insensitive). -1 if absent/bad.
long content_length(const std::string& head) {
    std::string lower;
    lower.reserve(head.size());
    for (char c : head) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    const std::size_t k = lower.find("content-length:");
    if (k == std::string::npos) return -1;
    std::size_t i = k + 15;
    while (i < head.size() && std::isspace(static_cast<unsigned char>(head[i]))) ++i;
    long v = 0;
    bool any = false;
    while (i < head.size() && std::isdigit(static_cast<unsigned char>(head[i]))) {
        v = v * 10 + (head[i] - '0');
        any = true;
        if (v > static_cast<long>(kMaxRequest)) return static_cast<long>(kMaxRequest);
        ++i;
    }
    return any ? v : -1;
}

// Read a bounded request: headers, then exactly Content-Length body bytes.
std::string read_request(int fd, bool& ok) {
    std::string buf;
    char        tmp[8192];
    bool        have_headers = false;
    std::size_t need = 0;
    ok = false;

    while (buf.size() < kMaxRequest) {
        const ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n < 0) { if (errno == EINTR) continue; return buf; }
        if (n == 0) break;                                    // peer closed
        buf.append(tmp, static_cast<std::size_t>(n));

        if (!have_headers) {
            const std::size_t hpos = buf.find("\r\n\r\n");
            if (hpos != std::string::npos) {
                have_headers = true;
                const long cl = content_length(buf.substr(0, hpos));
                need = hpos + 4 + static_cast<std::size_t>(cl < 0 ? 0 : cl);
                if (need > kMaxRequest) return buf;            // too large → caller 400s
            }
        }
        if (have_headers && buf.size() >= need) {
            buf.resize(need);
            ok = true;
            return buf;
        }
    }
    ok = have_headers;     // GETs (no body) complete as soon as headers arrive
    return buf;
}

bool write_all(int fd, const uint8_t* p, std::size_t n) {
    std::size_t sent = 0;
    while (sent < n) {
        const ssize_t k = send(fd, p + sent, n - sent, 0);
        if (k <= 0) { if (k < 0 && errno == EINTR) continue; return false; }
        sent += static_cast<std::size_t>(k);
    }
    return true;
}

} // namespace

int serve(const std::string& host, int port, const Handler& handler) {
    std::signal(SIGPIPE, SIG_IGN);   // writing to a closed socket must not kill us

    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { std::perror("socket"); return 1; }

    int opt = 1;
    ::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);        // fall back to localhost

    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(s);
        return 1;
    }
    if (::listen(s, 16) < 0) {
        std::perror("listen");
        ::close(s);
        return 1;
    }
    std::printf("webserver: serving on http://%s:%d/\n", host.c_str(), port);
    std::fflush(stdout);

    for (;;) {
        const int c = ::accept(s, nullptr, nullptr);
        if (c < 0) { if (errno == EINTR) continue; break; }

        bool              ok  = false;
        const std::string raw = read_request(c, ok);
        Response          resp;
        if (!ok) {
            resp = make_response(400, "Bad Request", "bad request\n");
        } else if (auto req = parse_request(raw)) {
            resp = handler(*req);
        } else {
            resp = make_response(400, "Bad Request", "bad request\n");
        }
        const std::vector<uint8_t> bytes = serialize(resp);
        write_all(c, bytes.data(), bytes.size());
        ::close(c);
    }
    ::close(s);
    return 0;
}

} // namespace web
