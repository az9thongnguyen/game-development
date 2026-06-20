// =============================================================================
//  server/static_files.cpp  —  path resolution + file reading
// =============================================================================
#include "server/static_files.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

namespace web {
namespace {

constexpr const char* kIndex = "demo.html";

// Percent-decode (%XX). Done BEFORE the traversal check so encoded "%2e%2e"
// can't sneak a ".." past us.
std::string percent_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() &&
            std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            const std::string hex = s.substr(i + 1, 2);
            out.push_back(static_cast<char>(std::stoi(hex, nullptr, 16)));
            i += 2;
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

bool has_unsafe_segment(const std::string& path) {
    if (path.find('\0') != std::string::npos) return true;   // NUL injection
    if (path.find('\\') != std::string::npos) return true;   // backslash
    // Reject any ".." path segment (the traversal vector).
    std::istringstream in(path);
    std::string        seg;
    while (std::getline(in, seg, '/'))
        if (seg == "..") return true;
    return false;
}

} // namespace

std::optional<std::string> resolve(const std::string& root, const std::string& target) {
    // Drop a query string and any fragment.
    std::string path = target.substr(0, target.find_first_of("?#"));
    path = percent_decode(path);

    // Strip leading slashes → relative to root.
    std::size_t start = 0;
    while (start < path.size() && path[start] == '/') ++start;
    path = path.substr(start);

    if (path.empty()) path = kIndex;           // "/" → index
    if (has_unsafe_segment(path)) return std::nullopt;

    return root + "/" + path;
}

std::optional<std::vector<uint8_t>> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size < 0) return std::nullopt;
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    if (size > 0) f.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

} // namespace web
