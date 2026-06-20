// =============================================================================
//  engine/assets.cpp  —  asset I/O implementation (standard C++ streams)
// =============================================================================
#include "engine/assets.hpp"

#include <fstream>

namespace assets {
namespace {
std::string g_base = ".";
}

void set_base_path(const std::string& base) {
    g_base = base.empty() ? "." : base;
}

std::optional<std::vector<uint8_t>> load_file(const std::string& path) {
    const std::string full = g_base + "/" + path;

    std::ifstream f(full, std::ios::binary);
    if (!f) {
        return std::nullopt;  // missing/unreadable → caller decides what to do
    }

    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size < 0) {
        return std::nullopt;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (size > 0) {
        f.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    return bytes;
}

bool write_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    const std::string full = g_base + "/" + path;

    std::ofstream f(full, std::ios::binary | std::ios::trunc);
    if (!f) {
        return false;  // unwritable location (missing dir, permissions, …)
    }
    if (!bytes.empty()) {
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(f);  // false if the stream errored mid-write
}

} // namespace assets
