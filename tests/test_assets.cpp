// =============================================================================
//  tests/test_assets.cpp  —  asset cache + hot reload (uses a temp base path)
// =============================================================================
#include "engine/asset_cache.hpp"
#include "engine/assets.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

struct Doc {
    std::string text;
};

static std::vector<uint8_t> bytes_of(const std::string& s) {
    return std::vector<uint8_t>(s.begin(), s.end());
}

int main() {
    assets::set_base_path("/tmp");
    const std::string path = "he_asset_doc_test.txt";

    // A loader that parses bytes → Doc, failing on empty input.
    assets::AssetCache cache;
    cache.register_loader<Doc>([](const std::vector<uint8_t>& b) -> std::shared_ptr<Doc> {
        if (b.empty()) return nullptr;                         // parse failure
        return std::make_shared<Doc>(Doc{std::string(b.begin(), b.end())});
    });

    // ---- load + cache identity ----
    CHECK(assets::write_file(path, bytes_of("hello")));
    auto d1 = cache.load<Doc>(path);
    CHECK(d1 && d1->text == "hello");
    auto d2 = cache.load<Doc>(path);
    CHECK(d2 == d1 && cache.size() == 1);                      // cached: same instance

    // ---- hot reload: in-place content swap (d1 still valid, sees new data) ----
    CHECK(assets::write_file(path, bytes_of("world")));
    CHECK(cache.reload(path));
    CHECK(d1->text == "world");                                // the held pointer updated

    // ---- mtime present/absent ----
    CHECK(assets::mtime(path) > 0);
    CHECK(assets::mtime("definitely_missing_xyz.bin") == 0);

    // ---- reload_changed no-ops when unchanged ----
    cache.reload(path);                                        // sync stored mtime
    CHECK(cache.reload_changed() == 0);

    // ---- error paths ----
    CHECK(cache.load<Doc>("missing_file_abc.txt") == nullptr); // missing
    CHECK(cache.load<int>(path) == nullptr);                   // no loader for int
    CHECK(assets::write_file("he_asset_empty_test.txt", {}));
    CHECK(cache.load<Doc>("he_asset_empty_test.txt") == nullptr);  // parse failure (empty)

    // ---- cleanup ----
    std::remove("/tmp/he_asset_doc_test.txt");
    std::remove("/tmp/he_asset_empty_test.txt");

    if (g_failures == 0) std::printf("assets: all tests passed\n");
    else                 std::printf("assets: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
