// =============================================================================
//  engine/assets.cpp  —  asset I/O implementation (standard C++ streams)
// =============================================================================
#include "engine/assets.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace assets {
namespace {
std::string g_base = ".";

// On the web, the preloaded asset tree is MEMFS — it lives in the tab and dies with
// it. `saves/` is mounted on IDBFS by the page before main() runs (see web/shell.html),
// and IDBFS only reaches the browser's database when something asks it to. So every
// write under saves/ asks.
//
// This is the one place that can know: assets:: is already the single door for file
// I/O, so a flush here covers the game's save, the sync bookmark, the device id and
// anything added later, with no caller remembering to do it.
void persist(const std::string& path) {
#ifdef __EMSCRIPTEN__
    if (path.rfind("saves/", 0) != 0) return;   // only the mounted subtree is durable
    // Serialised and coalesced, not fired per write. A single save is already several
    // writes — the file, the autosave that is cleared with it, the sync bookmark — and
    // overlapping FS.syncfs calls do not queue: they interleave, and the copy that
    // reaches the browser's database is whichever snapshot the last one happened to
    // take. Measured, not assumed: with one call per write, `slot1.sav` came back
    // ZERO BYTES on the next load, and the game only looked like it remembered because
    // the cloud copy was quietly filling in for it.
    EM_ASM({
        if (Module.__fsSyncing) { Module.__fsSyncAgain = 1; return; }
        Module.__fsSyncing = 1;
        var again = function () {
            FS.syncfs(false, function (err) {
                if (err) console.error('syncfs failed: ' + err);
                if (Module.__fsSyncAgain) { Module.__fsSyncAgain = 0; again(); }
                else                      { Module.__fsSyncing = 0; }
            });
        };
        again();
    });
#else
    (void)path;
#endif
}
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
    if (!f) return false;   // stream errored mid-write
    f.close();
    persist(path);
    return true;
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
    if (!f) return false;
    f.close();
    persist(path);
    return true;
}

bool rename(const std::string& from, const std::string& to) {
    const std::filesystem::path a = std::filesystem::path(g_base) / from;
    const std::filesystem::path b = std::filesystem::path(g_base) / to;
    std::error_code ec;
    if (b.has_parent_path()) std::filesystem::create_directories(b.parent_path(), ec);
    std::filesystem::rename(a, b, ec);   // atomic on the same filesystem
    if (ec) return false;
    persist(to);
    return true;
}

std::vector<std::string> list_dir(const std::string& dir, const std::string& suffix) {
    std::vector<std::string> names;
    std::error_code ec;                       // no exceptions: a missing dir is an answer
    const std::filesystem::path full = std::filesystem::path(g_base) / dir;
    for (const auto& e : std::filesystem::directory_iterator(full, ec)) {
        if (!e.is_regular_file()) continue;
        std::string n = e.path().filename().string();
        if (suffix.size() > n.size()) continue;
        if (n.compare(n.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        names.push_back(std::move(n));
    }
    std::sort(names.begin(), names.end());    // determinism, not tidiness — see the header
    return names;
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
