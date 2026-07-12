// =============================================================================
//  engine/assets.cpp  —  asset I/O implementation (standard C++ streams)
// =============================================================================
#include "engine/assets.hpp"

#include <filesystem>
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
    const std::filesystem::path full = std::filesystem::path(g_base) / path;

    // Create any missing parent directories (e.g. a content-addressed release path
    // like "releases/<hash>/package.txt"). std::ofstream will not make them itself;
    // works on native and Emscripten MEMFS alike. On failure the open below fails too.
    std::error_code ec;
    if (full.has_parent_path()) std::filesystem::create_directories(full.parent_path(), ec);

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

bool append_file(const std::string& path, const std::vector<uint8_t>& bytes) {
    const std::filesystem::path full = std::filesystem::path(g_base) / path;
    std::error_code ec;
    if (full.has_parent_path()) std::filesystem::create_directories(full.parent_path(), ec);

    std::ofstream f(full, std::ios::binary | std::ios::app);
    if (!f) return false;
    if (!bytes.empty()) {
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    return static_cast<bool>(f);
}

bool rename(const std::string& from, const std::string& to) {
    const std::filesystem::path a = std::filesystem::path(g_base) / from;
    const std::filesystem::path b = std::filesystem::path(g_base) / to;
    std::error_code ec;
    if (b.has_parent_path()) std::filesystem::create_directories(b.parent_path(), ec);
    std::filesystem::rename(a, b, ec);   // atomic on the same filesystem
    return !ec;
}

std::int64_t mtime(const std::string& path) {
    const std::string full = g_base + "/" + path;
    std::error_code   ec;
    const auto        t = std::filesystem::last_write_time(full, ec);  // no-throw overload
    if (ec) return 0;                                                  // missing / unsupported
    // NOTE: file_time_type's tick type can be wider than int64_t (libc++ uses a
    // 128-bit rep); narrowing to int64_t is safe for real file times (nanoseconds fit
    // until year 2262) and we only ever compare values for equality.
    return static_cast<std::int64_t>(t.time_since_epoch().count());
}

} // namespace assets
