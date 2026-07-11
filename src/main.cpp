// =============================================================================
//  main.cpp  —  engine entry point + mode dispatch
// =============================================================================
//  Usage:
//    demo                 -> M0 engine demo (retro 480x270 window)
//    demo --gui [hvh|hvai] [easy|medium|hard]  -> chess (large, crisp window)
//    demo --tui [hvh|hvai] [easy|medium|hard]  -> chess in the terminal
// =============================================================================
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/app.hpp"
#include "engine/assets.hpp"
#include "engine/project/project.hpp"
#include "engine/release/release.hpp"
#include "engine/resource/resource.hpp"
#include "platform/platform.hpp"

#include "demo/demo_scene.hpp"
#include "games/chess/chess_gui.hpp"
#include "games/chess/chess_tui.hpp"
#include "games/fps/raycast_scene.hpp"
#include "games/viz3d/scene3d.hpp"
#include "games/viz3d/editor_scene.hpp"
#include "games/iso/iso_scene.hpp"
#include "games/editor/editor_scene.hpp"
#include "games/colony/colony_scene.hpp"
#include "games/studio/studio_scene.hpp"
#include "games/sandbox/sandbox_scene.hpp"
#include "games/maplab/maplab_scene.hpp"
#include "games/anim/anim_scene.hpp"
#include "games/audio/audio_scene.hpp"
#include "games/fx/fx_scene.hpp"
#include "games/light/light_scene.hpp"

#include <chrono>
#include <cstdio>
#include <thread>

#include "gbaas/client.h"
#include "games/runner/worker.hpp"

namespace {

void parse_chess_opts(int argc, char** argv, int from, bool& vs_ai, int& depth) {
    for (int i = from; i < argc; ++i) {
        const std::string a = argv[i];
        if      (a == "hvh")    vs_ai = false;
        else if (a == "hvai")   vs_ai = true;
        else if (a == "easy")   depth = 2;
        else if (a == "medium") depth = 4;
        else if (a == "hard")   depth = 6;
    }
}

// SSAA factor for 2D scenes. Native gets full 2× supersampling; the web build
// stays at 1× (per-primitive AA + AA fonts still apply) because 4× software fill
// in WASM is too costly for a smooth frame rate.
#ifdef __EMSCRIPTEN__
constexpr int kAA = 1;
#else
constexpr int kAA = 2;
#endif

int run_window(const platform::Config& cfg, std::unique_ptr<engine::Scene> scene) {
    if (!platform::init(cfg)) return 1;
    platform::init_audio();

    engine::App app(std::move(scene));
    platform::run([&app](double dt) { app.frame(dt); });

    platform::shutdown();
    return 0;
}

// Launch a reference-game entry scene by its manifest id. Single source of truth
// shared by the --fps flag and the --project path, so a project manifest selects a
// game without editing this dispatch. Extend the table (and kKnownEntries) when a
// new entry scene is added. ponytail: starts with the one Horizon 0 reference game.
int launch_entry(const std::string& entry) {
    if (entry == "fps") {
        platform::Config cfg;
        cfg.title     = "hand-engine — fps";
        cfg.fb_width  = 640;
        cfg.fb_height = 400;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        return run_window(cfg, std::make_unique<fps::RaycastScene>());
    }
    std::fprintf(stderr, "unknown entry scene: %s\n", entry.c_str());
    return 1;
}

// The entry ids a project manifest may name — kept in sync with launch_entry.
const std::vector<std::string> kKnownEntries = {"fps"};

// Resolve a project's declared assets to packaged resources, each content-hashed
// through the assets:: seam. Any asset that does not resolve is appended to
// `missing`. This is the dependency-closure walk shared by inspect/launch/package/publish.
void collect_resources(const engine::Project& proj,
                       std::vector<engine::PackagedResource>& resources,
                       std::vector<std::string>& missing) {
    for (const auto& a : proj.assets) {
        if (auto ab = assets::load_file(a.path))
            resources.push_back({a.type, a.path,
                                 engine::content_hash(std::vector<uint8_t>(ab->begin(), ab->end()))});
        else
            missing.push_back(a.path);
    }
}

// A project that loaded, parsed, validated, and fully resolved its content closure.
struct Resolved {
    engine::Project                       proj;
    std::vector<engine::PackagedResource> resources;
};

// Strict load: read → parse → validate → resolve closure. On ANY problem prints it
// to stderr and returns nullopt. Callers that need a shippable project (launch,
// package, publish) go through here; only the inspect report walks the parts by hand.
std::optional<Resolved> resolve_project(const std::string& path) {
    auto bytes = assets::load_file(path);
    if (!bytes) { std::fprintf(stderr, "project: cannot read '%s'\n", path.c_str()); return std::nullopt; }
    auto proj = engine::parse_project(std::string(bytes->begin(), bytes->end()));
    if (!proj) {
        std::fprintf(stderr, "project: '%s' is not a valid gameproject1 manifest\n", path.c_str());
        return std::nullopt;
    }
    const auto errs = engine::validate(*proj, kKnownEntries);
    std::vector<engine::PackagedResource> resources;
    std::vector<std::string> missing;
    collect_resources(*proj, resources, missing);
    if (!errs.empty() || !missing.empty()) {
        std::fprintf(stderr, "project: '%s' is not shippable:\n", path.c_str());
        for (const auto& e : errs)    std::fprintf(stderr, "  - %s\n", e.c_str());
        for (const auto& m : missing) std::fprintf(stderr, "  - missing asset: %s\n", m.c_str());
        return std::nullopt;
    }
    return Resolved{std::move(*proj), std::move(resources)};
}

// Load a game.project manifest, and either print an inspection report (headless, no
// window) or launch its entry scene. Inspect walks the parts by hand so it can report
// partial/missing content; launch uses the strict resolver.
int launch_project(const std::string& path, bool inspect_only) {
    if (inspect_only) {
        auto bytes = assets::load_file(path);
        if (!bytes) { std::fprintf(stderr, "project: cannot read '%s'\n", path.c_str()); return 1; }
        auto proj = engine::parse_project(std::string(bytes->begin(), bytes->end()));
        if (!proj) {
            std::fprintf(stderr, "project: '%s' is not a valid gameproject1 manifest\n", path.c_str());
            return 1;
        }
        const auto errs = engine::validate(*proj, kKnownEntries);
        std::vector<engine::PackagedResource> resources;
        std::vector<std::string> missing;
        collect_resources(*proj, resources, missing);
        std::printf("project: %s\n  name   %s\n  schema %d\n  entry  %s\n",
                    path.c_str(), proj->name.c_str(), proj->schema, proj->entry.c_str());
        for (const auto& r : resources)
            std::printf("  asset  %-8s %s  [%s]\n",
                        r.type.c_str(), r.path.c_str(), engine::hash_hex(r.hash).c_str());
        for (const auto& m : missing)
            std::printf("  asset  MISSING  %s\n", m.c_str());
        if (errs.empty() && missing.empty()) { std::printf("  status OK\n"); return 0; }
        std::printf("  status %zu problem(s):\n", errs.size() + missing.size());
        for (const auto& e : errs)    std::printf("    - %s\n", e.c_str());
        for (const auto& m : missing) std::printf("    - missing asset: %s\n", m.c_str());
        return 1;
    }
    auto r = resolve_project(path);
    if (!r) return 1;
    return launch_entry(r->proj.entry);
}

// Emit the deterministic package manifest (identity + content-hashed resources +
// combined package hash) for a project to stdout. Refuses a project that would not launch.
int package_project(const std::string& path) {
    auto r = resolve_project(path);
    if (!r) return 1;
    std::printf("%s", engine::build_package(r->proj.name, r->proj.schema, r->proj.entry,
                                            r->resources).c_str());
    return 0;
}

// Publish: package a project and store its manifest immutably by content hash, then
// point a channel (default "preview") at it. Idempotent — re-publishing identical
// content is a verified no-op; a differing manifest at the same release id is refused
// (an immutable release's bytes never change). Writes through the assets:: seam, so
// natively it lands under assets/releases and assets/channels.
int publish_project(const std::string& path, const std::string& channel) {
    if (!engine::valid_channel_name(channel)) {
        std::fprintf(stderr, "release: invalid channel name '%s'\n", channel.c_str());
        return 1;
    }
    auto r = resolve_project(path);
    if (!r) return 1;

    const std::string pkg = engine::build_package(r->proj.name, r->proj.schema, r->proj.entry,
                                                  r->resources);
    const std::string hex   = engine::hash_hex(engine::package_hash(r->resources));  // == pkg's packagehash
    const std::string mpath = engine::release_manifest_path(hex);
    const std::vector<uint8_t> pkg_bytes(pkg.begin(), pkg.end());

    bool verified = false;
    if (auto existing = assets::load_file(mpath)) {
        if (*existing != pkg_bytes) {   // same id, different bytes → corruption/collision; never overwrite
            std::fprintf(stderr, "release: %s already stored with different bytes — refusing to overwrite\n",
                         hex.c_str());
            return 1;
        }
        verified = true;                // immutable release already present and byte-identical
    } else if (!assets::write_file(mpath, pkg_bytes)) {
        std::fprintf(stderr, "release: cannot write %s\n", mpath.c_str());
        return 1;
    }

    const std::string cser = engine::serialize_channel(hex);
    if (!assets::write_file(engine::channel_path(channel),
                            std::vector<uint8_t>(cser.begin(), cser.end()))) {
        std::fprintf(stderr, "release: cannot update channel '%s'\n", channel.c_str());
        return 1;
    }
    std::printf("%s %s\n  release %s\n  channel %s -> %s\n",
                verified ? "verified" : "published", r->proj.name.c_str(),
                hex.c_str(), channel.c_str(), hex.c_str());
    return 0;
}

// Read a channel's current release id (validated), or nullopt if unset/malformed.
std::optional<std::string> read_channel(const std::string& channel) {
    auto bytes = assets::load_file(engine::channel_path(channel));
    if (!bytes) return std::nullopt;
    return engine::parse_channel(std::string(bytes->begin(), bytes->end()));
}

// Promote: point the target channel at whatever release the source channel currently
// holds (e.g. preview -> production). The release must exist in the store — no
// dangling promotion.
int promote_release(const std::string& from, const std::string& to) {
    if (!engine::valid_channel_name(from) || !engine::valid_channel_name(to)) {
        std::fprintf(stderr, "release: invalid channel name\n");
        return 1;
    }
    auto hex = read_channel(from);
    if (!hex) { std::fprintf(stderr, "release: channel '%s' is unset or malformed\n", from.c_str()); return 1; }
    if (!assets::load_file(engine::release_manifest_path(*hex))) {
        std::fprintf(stderr, "release: channel '%s' points at missing release %s\n",
                     from.c_str(), hex->c_str());
        return 1;
    }
    const std::string cser = engine::serialize_channel(*hex);
    if (!assets::write_file(engine::channel_path(to),
                            std::vector<uint8_t>(cser.begin(), cser.end()))) {
        std::fprintf(stderr, "release: cannot update channel '%s'\n", to.c_str());
        return 1;
    }
    std::printf("promoted %s -> %s (%s)\n", from.c_str(), to.c_str(), hex->c_str());
    return 0;
}

// Rollback: point a channel at an explicit prior release id, which must exist in the
// store. The id comes from an operator (from --release-status or a prior publish), so
// it is validated as a trust-boundary input before it becomes a path.
int rollback_channel(const std::string& channel, const std::string& hex) {
    if (!engine::valid_channel_name(channel)) {
        std::fprintf(stderr, "release: invalid channel name '%s'\n", channel.c_str());
        return 1;
    }
    if (!engine::valid_hash_hex(hex)) {
        std::fprintf(stderr, "release: invalid release id '%s'\n", hex.c_str());
        return 1;
    }
    if (!assets::load_file(engine::release_manifest_path(hex))) {
        std::fprintf(stderr, "release: no such release %s\n", hex.c_str());
        return 1;
    }
    const std::string cser = engine::serialize_channel(hex);
    if (!assets::write_file(engine::channel_path(channel),
                            std::vector<uint8_t>(cser.begin(), cser.end()))) {
        std::fprintf(stderr, "release: cannot update channel '%s'\n", channel.c_str());
        return 1;
    }
    std::printf("rolled back %s -> %s\n", channel.c_str(), hex.c_str());
    return 0;
}

// Status: print the release each well-known channel points at, and whether that
// release is present in the store. Reads fixed channel files — no directory scan
// (the "collection database" smell the strategy warns against).
int release_status() {
    static const char* const kChannels[] = {"preview", "production"};
    for (const char* ch : kChannels) {
        auto hex = read_channel(ch);
        if (!hex) { std::printf("%-11s unset\n", ch); continue; }
        const bool present = assets::load_file(engine::release_manifest_path(*hex)).has_value();
        std::printf("%-11s %s  [%s]\n", ch, hex->c_str(), present ? "present" : "MISSING");
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // Resolve asset paths under ./assets BEFORE any scene is constructed (scenes
    // load images/files in their constructors).
    assets::set_base_path("assets");

    const std::string mode = (argc > 1) ? argv[1] : "";

    if (mode == "--tui") {
        bool vs_ai = true; int depth = 4;
        parse_chess_opts(argc, argv, 2, vs_ai, depth);
        return chess::run_tui(vs_ai, depth);
    }

    if (mode == "--gui") {
        bool vs_ai = true; int depth = 4;
        parse_chess_opts(argc, argv, 2, vs_ai, depth);
        platform::Config cfg;
        cfg.title     = "hand-engine — chess";
        cfg.fb_width  = 980;   // large + crisp (1:1, smooth scaling, HiDPI)
        cfg.fb_height = 720;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<chess::ChessScene>(vs_ai, depth));
    }

    if (mode == "--fps") {
        return launch_entry("fps");
    }

    // Launch a game from a versioned game.project manifest (asset-relative path),
    // instead of a hard-coded scene flag: this is the Horizon 0 golden path.
    if (mode == "--project") {
        if (argc < 3) { std::fprintf(stderr, "usage: demo --project <path>\n"); return 1; }
        return launch_project(argv[2], /*inspect_only=*/false);
    }

    // Headless: validate a manifest and print an inspection report, no window.
    if (mode == "--project-inspect") {
        if (argc < 3) { std::fprintf(stderr, "usage: demo --project-inspect <path>\n"); return 1; }
        return launch_project(argv[2], /*inspect_only=*/true);
    }

    // Headless: emit the deterministic package manifest (release-id seed) to stdout.
    if (mode == "--project-package") {
        if (argc < 3) { std::fprintf(stderr, "usage: demo --project-package <path>\n"); return 1; }
        return package_project(argv[2]);
    }

    // Headless: package a project and store it immutably by content hash, pointing a
    // channel (default "preview") at it — the Horizon 1 local release store.
    if (mode == "--project-publish") {
        if (argc < 3) { std::fprintf(stderr, "usage: demo --project-publish <path> [channel]\n"); return 1; }
        return publish_project(argv[2], argc > 3 ? argv[3] : "preview");
    }

    // Headless: move the <to> channel onto the release the <from> channel holds.
    if (mode == "--release-promote") {
        if (argc < 4) { std::fprintf(stderr, "usage: demo --release-promote <from> <to>\n"); return 1; }
        return promote_release(argv[2], argv[3]);
    }

    // Headless: point <channel> back at an explicit prior release id.
    if (mode == "--release-rollback") {
        if (argc < 4) { std::fprintf(stderr, "usage: demo --release-rollback <channel> <release-id>\n"); return 1; }
        return rollback_channel(argv[2], argv[3]);
    }

    // Headless: print what each channel points at and whether the release is present.
    if (mode == "--release-status") {
        return release_status();
    }

    if (mode == "--3d") {
        platform::Config cfg;
        cfg.title     = "hand-engine — 3D core";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<viz3d::Scene3D>());
    }

    if (mode == "--viz3d") {
        platform::Config cfg;
        cfg.title     = "hand-engine — viz3d sandbox";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<viz3d::EditorScene>());
    }

    if (mode == "--iso") {
        platform::Config cfg;
        cfg.title     = "hand-engine — iso farm sim";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<iso::IsoScene>());
    }

    if (mode == "--editor") {
        platform::Config cfg;
        cfg.title     = "hand-engine — editor (UI + physics)";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<editor::EditorScene>());
    }

    if (mode == "--colony") {
        platform::Config cfg;
        cfg.title     = "hand-engine — colony (ECS + jobs + UI)";
        cfg.fb_width  = 1000;
        cfg.fb_height = 760;   // room for the taller design-system panel
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<colony::ColonyScene>());
    }

    if (mode == "--studio") {
        platform::Config cfg;
        cfg.title     = "hand-engine — texture lab";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<studio::StudioScene>());
    }

    if (mode == "--sandbox") {
        platform::Config cfg;
        cfg.title     = "hand-engine — sandbox";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<sandbox::SandboxScene>());
    }

    if (mode == "--maplab") {
        platform::Config cfg;
        cfg.title     = "hand-engine — map lab";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<maplab::MaplabScene>());
    }

    if (mode == "--fx") {
        platform::Config cfg;
        cfg.title     = "hand-engine — particle fx";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<fx::FxScene>());
    }

    if (mode == "--light") {
        platform::Config cfg;
        cfg.title     = "hand-engine — 2D lighting";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = 1;   // lights are soft; skip SSAA to keep the per-pixel add cheap
        return run_window(cfg, std::make_unique<lightdemo::LightScene>());
    }

    if (mode == "--audio") {
        platform::Config cfg;
        cfg.title     = "hand-engine — audio mixer";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<audiodemo::AudioScene>());
    }

    if (mode == "--anim") {
        platform::Config cfg;
        cfg.title     = "hand-engine — sprite animation";
        cfg.fb_width  = 960;
        cfg.fb_height = 600;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;
        return run_window(cfg, std::make_unique<animdemo::AnimScene>());
    }

    // Headless test-run worker: polls a BaaS coordinator, runs claimed sandbox
    // scenarios, and posts results. Links the engine + SDK (the BaaS may not) — a
    // plain poll loop, not a windowed scene, so it bypasses platform::init.
    if (mode == "--runner") {
        if (argc < 4) {
            std::fprintf(stderr, "usage: demo --runner <base_url> <api_key>\n");
            return 1;
        }
        gbaas::Client c({argv[2], argv[3]});
        std::fprintf(stderr, "runner: polling %s for test runs (Ctrl-C to stop)\n", argv[2]);
        for (;;) {
            if (!runner::process_one(c))
                std::this_thread::sleep_for(std::chrono::seconds(1));  // idle: back off
        }
    }

    // No args: the M0 engine demo (retro 480x270, nearest scaling).
    platform::Config cfg;
    cfg.title     = "hand-engine — M0";
    cfg.fb_width  = 480;
    cfg.fb_height = 270;
    cfg.scale     = 2;   // retro: nearest scaling, no SSAA (keep the chunky M0 look)
    return run_window(cfg, std::make_unique<demo::DemoScene>());
}
