// =============================================================================
//  tests/test_farm_scene.cpp  —  the farm, played with no window
// =============================================================================
//  A Scene sees only a platform::InputState and a Renderer2D, so the game can be
//  driven headless: press the keys, check the world changed, and check the SCREEN
//  changed with it. The second half matters as much as the first — a simulation that
//  advances while the render ignores it looks exactly like a frozen game.
// =============================================================================
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gbaas/gbaas.h"

#include "engine/assets.hpp"
#include "engine/renderer2d.hpp"
#include "engine/text/font.hpp"
#include "engine/document/save.hpp"
#include "games/farm/farm_scene.hpp"
#include "games/farm/world.hpp"

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

constexpr int LW = 640, LH = 360, SS = 2;
constexpr int PW = LW * SS, PH = LH * SS;

void dump_ppm(const std::vector<std::uint32_t>& buf, const char* name) {
    if (FILE* f = std::fopen(name, "wb")) {
        std::fprintf(f, "P6\n%d %d\n255\n", PW, PH);
        for (auto p : buf) {
            const unsigned char rgb[3] = {static_cast<unsigned char>((p >> 16) & 0xFF),
                                          static_cast<unsigned char>((p >> 8) & 0xFF),
                                          static_cast<unsigned char>(p & 0xFF)};
            std::fwrite(rgb, 1, 3, f);
        }
        std::fclose(f);
    }
}

// Mean luminance of the play area (below the HUD strip), for the day/night check.
double brightness(const std::vector<std::uint32_t>& b) {
    double sum = 0;
    std::size_t n = 0;
    for (int y = 40 * SS; y < PH; ++y)
        for (int x = 0; x < PW; ++x) {
            const std::uint32_t p = b[static_cast<std::size_t>(y) * PW + x];
            sum += ((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF);
            ++n;
        }
    return n ? sum / static_cast<double>(n) : 0.0;
}

// Pixels in a logical rect that are not the most common colour there. Counting beats
// probing a coordinate: a single pixel in the middle of a widget is as likely to land
// on a deliberate hole as on the thing being measured — which has now happened three
// chapters running.
int ink(const std::vector<std::uint32_t>& b, int x, int y, int w, int h) {
    std::map<std::uint32_t, int> hist;
    for (int py = y * SS; py < (y + h) * SS && py < PH; ++py)
        for (int px = x * SS; px < (x + w) * SS && px < PW; ++px)
            ++hist[b[static_cast<std::size_t>(py) * PW + px]];
    std::uint32_t bg = 0;
    int           best = -1, total = 0;
    for (const auto& [c, n] : hist) { total += n; if (n > best) { best = n; bg = c; } }
    (void)bg;
    return total - best;
}

std::uint64_t fingerprint(const std::vector<std::uint32_t>& b) {
    std::uint64_t h = 1469598103934665603ull;
    for (auto p : b) h = (h ^ p) * 1099511628211ull;
    return h;
}

platform::InputState key(platform::Key k) {
    platform::InputState in{};
    in.key_pressed[static_cast<int>(k)] = true;
    in.key_down[static_cast<int>(k)] = true;
    return in;
}

// Answers requests from a table keyed by "METHOD /path", and records what was asked.
// Everything is deferred to poll(), like every real transport: the scene's callback
// chain (auth -> config -> events -> saves) therefore advances one step per frame,
// which is exactly how it behaves against a server.
struct ScriptedTransport : gbaas::ITransport {
    struct Reply { int status; std::string body; };
    std::map<std::string, Reply> routes;
    std::vector<std::string>     seen;      // "METHOD /path"
    std::vector<std::string>     bodies;    // parallel to `seen`
    std::string                  hold;      // this key is answered only on release()

    void send(const std::string& method, const std::string& url, const gbaas::Headers&,
              const std::string& body, gbaas::HttpDone done) override {
        const auto  slash = url.find("/v1/");
        std::string key   = method + " " + (slash == std::string::npos ? url : url.substr(slash));
        seen.push_back(key);
        bodies.push_back(body);
        const auto it = routes.find(key);
        const Reply r = it == routes.end() ? Reply{404, R"({"error":{"code":"not_found"}})"}
                                           : it->second;
        if (!hold.empty() && key == hold) held_.push_back({std::move(done), r});
        else                              pending_.push_back({std::move(done), r});
    }
    // Let a held request finish. Holding one is how a test stands inside the window
    // where a request is in flight — the window a race lives in.
    void release() {
        for (auto& h : held_) pending_.push_back(std::move(h));
        held_.clear();
        hold.clear();
    }
    // Drained in REVERSE, on purpose. A network does not promise that two requests
    // sent together come back in that order, and code that quietly depends on it
    // works on localhost and fails on a train. Correct code here never has two of
    // these in flight at once, so reversing costs it nothing.
    void poll() override {
        auto batch = std::move(pending_);
        pending_.clear();
        for (auto it = batch.rbegin(); it != batch.rend(); ++it)
            it->first(gbaas::HttpResponse{it->second.status, it->second.body});
    }
    [[nodiscard]] int count(const std::string& key) const {
        int n = 0;
        for (const std::string& s : seen) if (s == key) ++n;
        return n;
    }
private:
    std::vector<std::pair<gbaas::HttpDone, Reply>> pending_, held_;
};

// "no save here", written as a file rather than deleted: the asset layer has no
// remove, and an empty file reads back as absent everywhere it matters.
void clear_file(const std::string& path) { assets::write_file(path, {}); }

void write_text(const std::string& path, const std::string& text) {
    assets::write_file(path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

// A save file for a world that is unmistakably not the default one.
std::string save_text(int day, int gold) {
    farm::World w;
    w.seed   = 4242;
    w.day    = day;
    w.gold   = gold;
    w.minute = farm::kDayStartMin + 120;
    return doc::to_text(farm::to_save(w));
}

const char* kGuestOk = R"({"user":{"user_id":9,"display_name":"guest","is_guest":true},"access_token":"tok"})";

} // namespace

int main() {
    assets::set_base_path(ASSET_ROOT "/assets");
    auto bytes = assets::load_file("fonts/Inter.ttf");
    CHECK(bytes.has_value());
    if (!bytes) return 1;
    auto font = text::Font::load_from_bytes(std::move(*bytes));
    CHECK(font != nullptr);
    if (!font) return 1;

    // The default constructor talks to whatever is on port 8080. A test must not, so
    // it says "no backend" explicitly rather than depending on nothing listening —
    // which is true in CI and false on the machine this is written on.
    // Deterministic start: this machine may well have a real save lying around from
    // playing the game, and the scene now RESUMES one.
    clear_file("saves/farm/slot1.sav");
    clear_file("saves/farm/slot1.sync");

    farm::FarmScene scene{farm::FarmScene::default_config(),
                          std::make_unique<gbaas::OfflineTransport>()};
    CHECK(scene.ready());
    if (!scene.ready()) { std::printf("  problem: %s\n", scene.problem().c_str()); return 1; }

    // The manifest's map and data files actually loaded into a playable world.
    CHECK(scene.world().energy == farm::kMaxEnergy);
    CHECK(scene.world().day == 1);
    CHECK(!scene.world().npcs.empty());              // Anna is on the map
    CHECK(scene.world().npcs[0].x != 0 || scene.world().npcs[0].y != 0);

    std::vector<std::uint32_t> buf(static_cast<std::size_t>(PW) * PH, 0);
    platform::Framebuffer fb{buf.data(), PW, PH, PW};
    const platform::InputState idle{};

    // Takes the scene explicitly. It used to close over `scene`, which meant the
    // screenshots taken further down — of OTHER scenes — were all pictures of this
    // one, and the "it is on the screen" claims were pictures of the wrong screen.
    const auto draw = [&](engine::Scene& s, const platform::InputState& in) {
        for (auto& p : buf) p = 0;
        gfx::Renderer2D r(fb, SS);
        const engine::Context ctx{r, in, 1.0 / 60.0, 0.0, 0.0, font.get()};
        s.render(ctx);
    };
    const auto render = [&](const platform::InputState& in) { draw(scene, in); };

    scene.update(1.0 / 60.0, idle);
    render(idle);
    const std::uint64_t first = fingerprint(buf);
    dump_ppm(buf, "farm_day.ppm");

    // The world is on screen: the play area is not one flat colour, and the HUD strip
    // at the top is a different thing from the field below it.
    {
        // Counting DISTINCT colours rather than probing fixed pixels: the world is
        // centred in the viewport, so where any particular tile lands depends on the
        // camera, and a probe would be testing the camera by accident.
        std::vector<std::uint32_t> seen;
        for (int y = 60 * SS; y < PH; y += 8 * SS)
            for (int x = 0; x < PW; x += 8 * SS) {
                const std::uint32_t p = buf[static_cast<std::size_t>(y) * PW + x];
                bool found = false;
                for (std::uint32_t s : seen) if (s == p) { found = true; break; }
                if (!found) seen.push_back(p);
            }
        CHECK(seen.size() >= 4);                    // grass, path, trees, water at least
        CHECK(brightness(buf) > 20.0);              // and not a black screen
    }

    // ---- farming with the keyboard ----
    // Face down, then hoe / water the tile in front. The tool selection and the action
    // are the same two keys a player presses.
    scene.update(1.0 / 60.0, key(platform::Key::S));
    scene.update(1.0 / 60.0, key(platform::Key::Num1));
    scene.update(1.0 / 60.0, key(platform::Key::Z));
    int tilled = 0;
    for (const auto& [k, s] : scene.world().soil) if (s.tilled) ++tilled;
    CHECK(tilled == 1);

    scene.update(1.0 / 60.0, key(platform::Key::Num2));
    scene.update(1.0 / 60.0, key(platform::Key::Z));
    int watered = 0;
    for (const auto& [k, s] : scene.world().soil) if (s.watered) ++watered;
    CHECK(watered == 1);

    scene.update(1.0 / 60.0, key(platform::Key::Num3));
    scene.update(1.0 / 60.0, key(platform::Key::Z));
    int planted = 0;
    for (const auto& [k, s] : scene.world().soil) if (s.crop >= 0) ++planted;
    CHECK(planted == 1);
    CHECK(scene.world().energy < farm::kMaxEnergy);

    // Working the field is VISIBLE. Without this the simulation could advance behind
    // a screen that never changed and every check above would still pass.
    render(idle);
    CHECK(fingerprint(buf) != first);
    dump_ppm(buf, "farm_planted.ppm");
    const double lit = brightness(buf);

    // ---- the art, and the tile that has none --------------------------------
    // The theme maps grass, path, trees, walls and stones onto Kenney's sheet; the
    // pond has no line, because Tiny Town has no water tile. So the frame must
    // contain the flat water colour and NONE of the other flat colours — which is
    // the per-id fallback stated as pixels rather than as an intention.
    {
        CHECK(scene.tile_count() == 132);          // 192x176 of 16px tiles
        render(idle);
        int grass = 0, path = 0, tree = 0, water = 0;
        for (std::uint32_t p : buf) {
            if (p == 0xFF4E7A3Cu) ++grass;         // the flat colours from before art
            if (p == 0xFF9A7B4Fu) ++path;
            if (p == 0xFF23482Au) ++tree;
            if (p == 0xFF2E6E8Eu) ++water;
        }
        CHECK(grass == 0);
        CHECK(path  == 0);
        CHECK(tree  == 0);
        CHECK(water > 0);                          // ...and the pond is still painted
        dump_ppm(buf, "farm_art.ppm");
    }


    // ---- night falls ----
    // Run the clock to the small hours and the world darkens. The tint is what makes
    // the clock a resource rather than a number in the corner.
    // Stop at 22:00 rather than running a fixed frame count: past 02:00 the player
    // collapses and the clock resets, which would leave this asserting on a fresh
    // morning and reporting the tint as broken.
    int frames = 0;
    while (scene.world().minute < 22 * 60 && frames < 200000) {
        scene.update(1.0 / 60.0, idle);
        ++frames;
    }
    CHECK(scene.world().minute >= 22 * 60);
    CHECK(scene.world().day == 1);            // ...and we never rolled over
    render(idle);
    dump_ppm(buf, "farm_night.ppm");
    CHECK(brightness(buf) < lit);


    // ---- the pointer picks the tile you work on -----------------------------
    // The farm is the first consumer of a mouse position in this project. The rule is
    // the keyboard's: an ADJACENT tile, and a click acts on it — so a pointer that is
    // off by a tile is visible as hoeing the wrong square.
    {
        clear_file("saves/farm/slot1.sav");
        clear_file("saves/farm/slot1.sync");
        farm::FarmScene sc{farm::FarmScene::default_config(),
                           std::make_unique<gbaas::OfflineTransport>()};
        CHECK(sc.ready());
        // render() is what tells the camera how big the screen is; the pointer is
        // mapped through the camera, so it needs one frame first.
        draw(sc, idle);
        const int px = sc.world().px, py = sc.world().py;

        const auto at_tile = [&](int tx, int ty, bool click) {
            platform::InputState in{};
            // The inverse of what the scene does: world pixel -> screen pixel.
            // Written out rather than reusing a helper on purpose — a test that shares
            // the arithmetic it is checking proves only that it is self-consistent.
            in.mouse_x = tx * 16 + 8 - static_cast<int>(sc.camera_origin_x());
            in.mouse_y = ty * 16 + 8 - static_cast<int>(sc.camera_origin_y());
            in.mouse_down[static_cast<int>(platform::MouseButton::Left)]    = click;
            in.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = click;
            return in;
        };

        // Hovering the tile to the right turns the player toward it.
        sc.update(1.0 / 60.0, at_tile(px + 1, py, false));
        draw(sc, idle);
        CHECK(sc.facing_x() == 1 && sc.facing_y() == 0);
        sc.update(1.0 / 60.0, at_tile(px, py + 1, false));
        CHECK(sc.facing_x() == 0 && sc.facing_y() == 1);

        // A tile two away is out of reach: the facing does not move to it.
        sc.update(1.0 / 60.0, at_tile(px + 2, py, false));
        CHECK(sc.facing_x() == 0 && sc.facing_y() == 1);

        // Clicking an adjacent tile works it — the same action Z performs.
        const std::size_t before = sc.world().soil.size();
        sc.update(1.0 / 60.0, at_tile(px, py + 1, true));
        CHECK(sc.world().soil.size() == before + 1);
        const long long key = farm::soil_key(px, py + 1);
        CHECK(sc.world().soil.count(key) == 1 && sc.world().soil.at(key).tilled);

        // No pointer means no clicking. NOTE what this does and does not prove: with
        // the whole farm fitting on screen the camera centres it, so screen (-1,-1)
        // maps far outside the player's four neighbours and the adjacency rule would
        // reject it even without the `mouse_x >= 0` guard. The guard is kept because
        // -1 means "no pointer" in the platform contract and reading it as a position
        // is how the Play viewport got its (0,0) bug in chapter 115 — but this
        // assertion is a contract check, not a proof that the guard is load-bearing.
        const std::size_t after = sc.world().soil.size();
        platform::InputState nomouse{};
        nomouse.mouse_x = nomouse.mouse_y = -1;
        nomouse.mouse_pressed[static_cast<int>(platform::MouseButton::Left)] = true;
        sc.update(1.0 / 60.0, nomouse);
        CHECK(sc.world().soil.size() == after);
    }

    // =========================================================================
    //  The cloud, without a cloud. Everything below runs through a scripted
    //  transport: the same callback chain the game runs against a server, with
    //  the server's answers written down here where they can be wrong on purpose.
    // =========================================================================

    // ---- offline is a state, not a failure ----
    for (int i = 0; i < 6; ++i) scene.update(1.0 / 60.0, idle);
    CHECK(!scene.online());
    CHECK(scene.cloud_line() == "offline");
    CHECK(scene.world().day == 1);              // ...and the game played fine above

    // ---- the hotbar shows which tool is held, and which are not ----
    // Four slots, and the selected one carries a border and brighter text. Asserted by
    // counting, because the interesting pixels are the outline, not the middle.
    {
        constexpr int kSlotW = 62, kSlotH = 24;
        const int     hy = LH - kSlotH - 8;
        const auto    slot = [&](int i) { return ink(buf, 8 + i * (kSlotW + 4), hy, kSlotW, kSlotH); };

        scene.update(1.0 / 60.0, key(platform::Key::Num1));
        render(idle);
        const int hoe_on = slot(0), water_off = slot(1);
        scene.update(1.0 / 60.0, key(platform::Key::Num2));
        render(idle);
        const int hoe_off = slot(0), water_on = slot(1);
        CHECK(hoe_on > hoe_off);          // the Hoe slot lost its border
        CHECK(water_on > water_off);      // ...and the Water slot gained one
        dump_ppm(buf, "farm_hotbar.ppm");
    }

    const auto routes_ok = [](ScriptedTransport& t) {
        t.routes["POST /v1/auth/guest"]        = {200, kGuestOk};
        t.routes["GET /v1/config/farm_defs"]   = {200, R"({"value":"crop parsnip sell=40\n"})"};
        t.routes["GET /v1/events"]             = {200, R"({"events":[]})"};
        t.routes["POST /v1/analytics/events"]  = {200, R"({"ok":true})"};
    };

    // ---- layers: file -> remote config -> live event, in that order ----------
    {
        clear_file("saves/farm/slot1.sav");
        clear_file("saves/farm/slot1.sync");
        auto owner = std::make_unique<ScriptedTransport>();
        ScriptedTransport* t = owner.get();
        routes_ok(*t);
        t->routes["GET /v1/events"] = {200,
            R"({"events":[{"key":"harvest_festival","name":"Harvest Festival","payload":"crop parsnip sell=90\n"}]})"};
        t->routes["GET /v1/saves/farm"] = {404, R"({"error":{"code":"not_found"}})"};

        farm::FarmScene sc{farm::FarmScene::default_config(), std::move(owner)};
        for (int i = 0; i < 8; ++i) sc.update(1.0 / 60.0, idle);

        CHECK(sc.online());
        CHECK(sc.event_name() == "Harvest Festival");
        // The event landed ON TOP of remote config, which landed on top of the file.
        // If those two were fired together this would be 40 about half the time.
        CHECK(sc.defs().crop("parsnip")->sell == 90);
        // ...and nothing else about the parsnip moved. This is the whole difference
        // between an override and merge_defs.
        CHECK(sc.defs().crop("parsnip")->days   == 4);
        CHECK(sc.defs().crop("parsnip")->stages == 5);
        CHECK(sc.defs().crop("parsnip")->seed   == 20);
        CHECK(sc.defs().crop("turnip")->sell    == 15);
        // Nothing local, nothing remote: there is nothing to sync and no upload.
        CHECK(t->count("PUT /v1/saves/farm") == 0);
        CHECK(sc.cloud_line() == "cloud empty");
    }

    // ---- local save, empty cloud -> push ------------------------------------
    {
        write_text("saves/farm/slot1.sav", save_text(/*day*/ 5, /*gold*/ 700));
        clear_file("saves/farm/slot1.sync");
        auto owner = std::make_unique<ScriptedTransport>();
        ScriptedTransport* t = owner.get();
        routes_ok(*t);
        t->routes["GET /v1/saves/farm"] = {404, R"({"error":{"code":"not_found"}})"};
        t->routes["PUT /v1/saves/farm"] = {200, R"({"slot":"farm","version":1,"size":12})"};

        farm::FarmScene sc{farm::FarmScene::default_config(), std::move(owner)};
        CHECK(sc.world().day == 5);             // resumed, rather than starting over
        CHECK(sc.world().gold == 700);
        for (int i = 0; i < 8; ++i) sc.update(1.0 / 60.0, idle);
        CHECK(t->count("PUT /v1/saves/farm") == 1);
        CHECK(sc.cloud_line() == "cloud v1");
        // The bookmark was written, so the NEXT run has something to compare against.
        const auto mark = assets::load_file("saves/farm/slot1.sync");
        CHECK(mark && !mark->empty());

        // Saving by hand pushes too — the local file is the record, the cloud a copy.
        sc.update(1.0 / 60.0, key(platform::Key::F5));
        for (int i = 0; i < 3; ++i) sc.update(1.0 / 60.0, idle);
        CHECK(t->count("PUT /v1/saves/farm") == 2);
    }

    // ---- both moved since they agreed -> ASK, and touch nothing -------------
    {
        write_text("saves/farm/slot1.sav", save_text(/*day*/ 5, /*gold*/ 700));
        clear_file("saves/farm/slot1.sync");    // never synced: this machine cannot claim to be unchanged
        auto owner = std::make_unique<ScriptedTransport>();
        ScriptedTransport* t = owner.get();
        routes_ok(*t);
        const std::string cloud = save_text(/*day*/ 11, /*gold*/ 3000);
        std::string escaped;
        for (char c : cloud) {
            if      (c == '\n') escaped += "\\n";
            else if (c == '"')  escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else                escaped += c;
        }
        t->routes["GET /v1/saves/farm"] = {200,
            R"({"slot":"farm","version":3,"data":")" + escaped + R"("})"};
        t->routes["PUT /v1/saves/farm"] = {200, R"({"slot":"farm","version":4,"size":12})"};

        farm::FarmScene sc{farm::FarmScene::default_config(), std::move(owner)};
        for (int i = 0; i < 8; ++i) sc.update(1.0 / 60.0, idle);

        CHECK(sc.conflict());
        CHECK(sc.world().day == 5);                       // the local evening is still on screen
        CHECK(t->count("PUT /v1/saves/farm") == 0);       // and nothing was uploaded over the cloud's
        CHECK(sc.cloud_line() == "two saves differ");

        // The conflict is on the SCREEN, not only in a flag.
        draw(sc, idle);
        dump_ppm(buf, "farm_conflict.ppm");

        // The chip stops reporting and starts asking — and names both keys, because
        // "two saves differ" leaves someone staring at a farm they cannot save.
        CHECK(sc.cloud_chip().find("F6") != std::string::npos);
        CHECK(sc.cloud_chip().find("F7") != std::string::npos);
        const int asking = ink(buf, LW / 2, 0, LW / 2, 20);

        // F7 takes the cloud's copy. Only now does the world change.
        sc.update(1.0 / 60.0, key(platform::Key::F7));
        CHECK(!sc.conflict());
        CHECK(sc.world().day == 11);
        CHECK(sc.world().gold == 3000);
        CHECK(sc.cloud_line() == "cloud v3");
        draw(sc, idle);
        CHECK(ink(buf, LW / 2, 0, LW / 2, 20) < asking);
        for (int i = 0; i < 3; ++i) sc.update(1.0 / 60.0, idle);
        CHECK(t->count("PUT /v1/saves/farm") == 0);       // taking a copy is not a reason to send one
    }

    // ---- the day ends: analytics go out, and the day is a save point --------
    {
        clear_file("saves/farm/slot1.sav");
        clear_file("saves/farm/slot1.sync");
        auto owner = std::make_unique<ScriptedTransport>();
        ScriptedTransport* t = owner.get();
        routes_ok(*t);
        t->routes["GET /v1/saves/farm"] = {404, R"({"error":{"code":"not_found"}})"};
        t->routes["PUT /v1/saves/farm"] = {200, R"({"slot":"farm","version":1,"size":12})"};
        farm::FarmScene sc{farm::FarmScene::default_config(), std::move(owner)};
        for (int i = 0; i < 8; ++i) sc.update(1.0 / 60.0, idle);
        const int pushes = t->count("PUT /v1/saves/farm");

        // Run the clock past 02:00 in a few large steps rather than 43,200 small ones.
        for (int i = 0; i < 10 && !sc.world().collapsed(); ++i) sc.update(100.0, idle);
        for (int i = 0; i < 6; ++i) sc.update(1.0 / 60.0, idle);
        CHECK(sc.world().day == 2);
        CHECK(t->count("POST /v1/analytics/events") >= 1);   // day_end reached the operator
        CHECK(t->count("PUT /v1/saves/farm") == pushes + 1); // ...and the day was saved

        // The body carries the day, not just the event name — an event with no
        // properties answers "did anything happen" and nothing else.
        bool has_day = false;
        for (std::size_t i = 0; i < t->seen.size(); ++i)
            if (t->seen[i] == "POST /v1/analytics/events" &&
                t->bodies[i].find("farm.day_end") != std::string::npos &&
                t->bodies[i].find("\"day\":2") != std::string::npos) has_day = true;
        CHECK(has_day);
    }

    // ---- saving while the first sync is still in flight ---------------------
    // The window is a couple of hundred milliseconds against a real server, which is
    // exactly long enough to press F5 in. Uploading here would send the save twice and
    // decide from a snapshot of the cloud older than the upload — found by the
    // end-to-end test, pinned here where the window can be held open on purpose.
    {
        write_text("saves/farm/slot1.sav", save_text(5, 700));
        clear_file("saves/farm/slot1.sync");
        auto owner = std::make_unique<ScriptedTransport>();
        ScriptedTransport* t = owner.get();
        routes_ok(*t);
        t->routes["GET /v1/saves/farm"] = {404, R"({"error":{"code":"not_found"}})"};
        t->routes["PUT /v1/saves/farm"] = {200, R"({"slot":"farm","version":1,"size":12})"};
        t->hold = "GET /v1/saves/farm";

        farm::FarmScene sc{farm::FarmScene::default_config(), std::move(owner)};
        for (int i = 0; i < 8; ++i) sc.update(1.0 / 60.0, idle);
        CHECK(t->count("GET /v1/saves/farm") == 1);      // asked, and still waiting

        sc.update(1.0 / 60.0, key(platform::Key::F5));
        for (int i = 0; i < 3; ++i) sc.update(1.0 / 60.0, idle);
        CHECK(t->count("PUT /v1/saves/farm") == 0);      // nothing sent from inside the window
        CHECK(sc.cloud_line() == "saved - syncing");

        // The verdict reads the FILE, which F5 has already written — so the save is not
        // lost, it is uploaded once, by the decision that knew about it.
        t->release();
        for (int i = 0; i < 4; ++i) sc.update(1.0 / 60.0, idle);
        CHECK(t->count("PUT /v1/saves/farm") == 1);
        CHECK(sc.cloud_line() == "cloud v1");
    }

    // ---- a conflict resolved the other way ----------------------------------
    {
        write_text("saves/farm/slot1.sav", save_text(5, 700));
        clear_file("saves/farm/slot1.sync");
        auto owner = std::make_unique<ScriptedTransport>();
        ScriptedTransport* t = owner.get();
        routes_ok(*t);
        t->routes["GET /v1/saves/farm"] = {200, R"({"slot":"farm","version":3,"data":"gamefarm\n"})"};
        t->routes["PUT /v1/saves/farm"] = {200, R"({"slot":"farm","version":4,"size":12})"};
        farm::FarmScene sc{farm::FarmScene::default_config(), std::move(owner)};
        for (int i = 0; i < 8; ++i) sc.update(1.0 / 60.0, idle);
        // An unreadable cloud save is NOT an empty slot: pushing over it would destroy
        // something a newer build might read perfectly well.
        CHECK(sc.cloud_line() == "cloud save unreadable");
        CHECK(t->count("PUT /v1/saves/farm") == 0);
    }

    // ---- a transport failure is not an empty slot ---------------------------
    {
        write_text("saves/farm/slot1.sav", save_text(5, 700));
        clear_file("saves/farm/slot1.sync");
        auto owner = std::make_unique<ScriptedTransport>();
        ScriptedTransport* t = owner.get();
        routes_ok(*t);
        t->routes["GET /v1/saves/farm"] = {500, R"({"error":{"code":"boom"}})"};
        t->routes["PUT /v1/saves/farm"] = {200, R"({"slot":"farm","version":9,"size":12})"};
        farm::FarmScene sc{farm::FarmScene::default_config(), std::move(owner)};
        for (int i = 0; i < 8; ++i) sc.update(1.0 / 60.0, idle);
        CHECK(sc.cloud_line() == "cloud unavailable");
        CHECK(t->count("PUT /v1/saves/farm") == 0);
    }

    // ---- an operator's typo reaches the person playing the build ------------
    {
        clear_file("saves/farm/slot1.sav");
        clear_file("saves/farm/slot1.sync");
        auto owner = std::make_unique<ScriptedTransport>();
        ScriptedTransport* t = owner.get();
        routes_ok(*t);
        t->routes["GET /v1/config/farm_defs"] = {200, R"({"value":"crop parsnip sel=40\n"})"};
        t->routes["GET /v1/saves/farm"]       = {404, R"({"error":{"code":"not_found"}})"};
        farm::FarmScene sc{farm::FarmScene::default_config(), std::move(owner)};
        for (int i = 0; i < 8; ++i) sc.update(1.0 / 60.0, idle);
        CHECK(sc.defs().crop("parsnip")->sell == 35);     // the file's number, unchanged
        CHECK(sc.config_problem().find("sel") != std::string::npos);
        draw(sc, idle);
        // ...and it is ON SCREEN, counted by its own chip rather than by hoping a
        // coordinate lands on a letter.
        int chip = 0;
        for (std::uint32_t p : buf) if (p == 0xFF301A20u) ++chip;
        CHECK(chip > 200);
        dump_ppm(buf, "farm_config_typo.ppm");
    }

    clear_file("saves/farm/slot1.sav");
    clear_file("saves/farm/slot1.sync");

    if (g_failures == 0) std::printf("farm_scene: all tests passed\n");
    else                 std::printf("farm_scene: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
