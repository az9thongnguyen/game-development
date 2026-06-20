// =============================================================================
//  server/http.cpp  —  HTTP/1.1 parse + serialize implementation
// =============================================================================
#include "server/http.hpp"

#include <cctype>
#include <sstream>

namespace web {
namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

std::string Request::header(const std::string& name) const {
    const std::string want = lower(name);
    for (const auto& h : headers)
        if (lower(h.first) == want) return h.second;
    return "";
}

std::optional<Request> parse_request(const std::string& raw) {
    // Split header section from body at the first blank line (CRLF or LF tolerant).
    std::size_t sep = raw.find("\r\n\r\n");
    std::size_t sep_len = 4;
    if (sep == std::string::npos) { sep = raw.find("\n\n"); sep_len = 2; }

    const std::string head = (sep == std::string::npos) ? raw : raw.substr(0, sep);
    std::string       body = (sep == std::string::npos) ? "" : raw.substr(sep + sep_len);

    // Split the header section into lines (strip trailing '\r').
    std::vector<std::string> lines;
    {
        std::istringstream in(head);
        std::string        line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(line);
        }
    }
    if (lines.empty() || lines[0].empty()) return std::nullopt;

    // Request line: METHOD SP TARGET SP VERSION  (exactly three tokens).
    Request req;
    {
        std::istringstream rl(lines[0]);
        if (!(rl >> req.method >> req.target >> req.version)) return std::nullopt;
        std::string extra;
        if (rl >> extra) return std::nullopt;          // more than 3 tokens → malformed
        if (req.method.empty() || req.target.empty()) return std::nullopt;
    }

    for (std::size_t i = 1; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        if (l.empty()) continue;
        const std::size_t colon = l.find(':');
        if (colon == std::string::npos) continue;       // skip malformed header lines
        req.headers.emplace_back(trim(l.substr(0, colon)), trim(l.substr(colon + 1)));
    }

    req.body = std::move(body);
    return req;
}

std::vector<uint8_t> serialize(const Response& r) {
    std::ostringstream h;
    h << "HTTP/1.1 " << r.status << ' ' << r.reason << "\r\n"
      << "Content-Type: " << r.content_type << "\r\n"
      << "Content-Length: " << r.body.size() << "\r\n"
      << "Connection: close\r\n"
      << "\r\n";
    const std::string head = h.str();
    std::vector<uint8_t> out(head.begin(), head.end());
    out.insert(out.end(), r.body.begin(), r.body.end());
    return out;
}

Response make_response(int status, const std::string& reason, const std::string& body,
                       const std::string& content_type) {
    Response r;
    r.status = status;
    r.reason = reason;
    r.content_type = content_type;
    r.body.assign(body.begin(), body.end());
    return r;
}

} // namespace web
