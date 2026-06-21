// =============================================================================
//  tests/test_assets.cpp  —  asset cache + hot reload (uses a temp base path)
// =============================================================================
#include "engine/asset_cache.hpp"
#include "engine/assets.hpp"
#include "engine/image.hpp"

#include <cstdint>
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

static void put_be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x >> 24)); v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 8));  v.push_back(static_cast<uint8_t>(x));
}

// Covers the .hrt loader's dimension cap (a security fix: huge w/h must be rejected,
// not overflow the byte-count check on 32-bit / cast to a negative int).
static void test_image_loader() {
    // valid 1x1 red pixel
    std::vector<uint8_t> ok = {'H','R','T','1'};
    put_be32(ok, 1); put_be32(ok, 1);
    ok.insert(ok.end(), {255, 0, 0, 255});           // RGBA
    CHECK(assets::write_file("he_img_ok.hrt", ok));
    auto img = gfx::load_image("he_img_ok.hrt");
    CHECK(img && img->w == 1 && img->h == 1);

    // absurd dimensions → rejected by the cap (would overflow on wasm32)
    std::vector<uint8_t> huge = {'H','R','T','1'};
    put_be32(huge, 0x40000000u); put_be32(huge, 0x40000000u);
    CHECK(assets::write_file("he_img_huge.hrt", huge));
    CHECK(!gfx::load_image("he_img_huge.hrt"));

    // truncated body (claims 2x2 but has no pixels) → rejected
    std::vector<uint8_t> trunc = {'H','R','T','1'};
    put_be32(trunc, 2); put_be32(trunc, 2);
    CHECK(assets::write_file("he_img_trunc.hrt", trunc));
    CHECK(!gfx::load_image("he_img_trunc.hrt"));

    CHECK(!gfx::load_image("he_img_missing.hrt"));   // missing file

    std::remove("/tmp/he_img_ok.hrt");
    std::remove("/tmp/he_img_huge.hrt");
    std::remove("/tmp/he_img_trunc.hrt");
}

int main() {
    assets::set_base_path("/tmp");
    test_image_loader();
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
