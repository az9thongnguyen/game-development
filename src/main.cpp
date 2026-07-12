// =============================================================================
//  main.cpp  —  engine entry point + mode dispatch
// =============================================================================
//  Usage:
//    demo                 -> M0 engine demo (retro 480x270 window)
//    demo --gui [hvh|hvai] [easy|medium|hard]  -> chess (large, crisp window)
//    demo --tui [hvh|hvai] [easy|medium|hard]  -> chess in the terminal
// =============================================================================
#include <ctime>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "engine/app.hpp"
#include "engine/assets.hpp"
#include "engine/hub/hub.hpp"
#include "engine/hub/hub_build.hpp"
#include "engine/project/project.hpp"
#include "engine/release/ops.hpp"
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
#include "games/hub/hub_scene.hpp"
#include "games/studio_shell/studio_shell_scene.hpp"

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

// Create (the first verb of the canonical loop): scaffold a new, valid, launchable
// game.project manifest — no hand-remembering the format. Reuses project_core::to_text,
// validates before writing (a scaffold that can't launch is a bug, not a starting point),
// and refuses to clobber an existing file. ponytail: the real create UX is the deferred
// Studio shell; this is the headless stand-in that completes the CLI golden path.
int new_project(const std::string& out_path, const std::string& entry, const std::string& name) {
    engine::Project p;
    p.name   = name;
    p.schema = engine::kProjectSchema;
    p.entry  = entry;
    const auto errs = engine::validate(p, kKnownEntries);   // known entry + non-empty name, fail-closed
    if (!errs.empty()) {
        std::fprintf(stderr, "project-new: cannot scaffold a launchable project:\n");
        for (const auto& e : errs) std::fprintf(stderr, "  - %s\n", e.c_str());
        return 1;
    }
    if (assets::load_file(out_path)) {   // creating, not overwriting
        std::fprintf(stderr, "project-new: '%s' already exists — refusing to overwrite\n", out_path.c_str());
        return 1;
    }
    const std::string text = engine::to_text(p);
    if (!assets::write_file(out_path, std::vector<uint8_t>(text.begin(), text.end()))) {
        std::fprintf(stderr, "project-new: cannot write '%s'\n", out_path.c_str());
        return 1;
    }
    std::printf("created %s\n  name  %s\n  entry %s\n"
                "  next  demo --project %s   (or --project-inspect / --project-publish)\n"
                "  add content by appending  asset <type> <path>  lines\n",
                out_path.c_str(), name.c_str(), entry.c_str(), out_path.c_str());
    return 0;
}

// Read a channel's current release id (validated), or nullopt if unset/malformed.
// (Still used by verify/status/log below; the write side lives in engine::release ops.)
std::optional<std::string> read_channel(const std::string& channel) {
    return engine::current_release(channel);
}

// The release verbs are thin CLI wrappers over the shared engine::release ops — the same
// functions the graphical Hub Scene calls. They print the structured message (stdout on
// success, stderr on failure) and map ok → exit 0.
int print_op(const engine::OpResult& r) {
    std::fprintf(r.ok ? stdout : stderr, "%s\n", r.message.c_str());
    return r.ok ? 0 : 1;
}
int publish_project(const std::string& path, const std::string& channel, const std::string& reason) {
    return print_op(engine::publish(path, channel, reason, kKnownEntries));
}
int promote_release(const std::string& from, const std::string& to, const std::string& reason) {
    return print_op(engine::promote(from, to, reason));
}
int rollback_channel(const std::string& channel, const std::string& hex, const std::string& reason) {
    return print_op(engine::rollback(channel, hex, reason));
}

// The channels with defined promotion semantics: publish lands in development, promote
// forward to preview (shareable) then production (live). Other names are still allowed
// ad hoc by publish/promote/rollback; these are just the ones --release-status reports.
const char* const kChannels[] = {"development", "preview", "production"};

// Status: print the release each well-known channel points at, and whether that
// release is present in the store. Reads fixed channel files — no directory scan
// (the "collection database" smell the strategy warns against).
int release_status() {
    for (const char* ch : kChannels) {
        auto hex = read_channel(ch);
        if (!hex) { std::printf("%-11s unset\n", ch); continue; }
        const bool present = assets::load_file(engine::release_manifest_path(*hex)).has_value();
        std::printf("%-11s %s  [%s]\n", ch, hex->c_str(), present ? "present" : "MISSING");
    }
    return 0;
}

// Log: print the append-only audit history (optionally filtered to one channel). Reads
// the log file forward — never scans the store directory.
int release_log(const std::string& channel_filter) {
    auto bytes = assets::load_file(engine::audit_log_path());
    if (!bytes) { std::printf("(no releases published yet)\n"); return 0; }
    std::istringstream in(std::string(bytes->begin(), bytes->end()));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto e = engine::parse_audit_line(line);
        if (!e) continue;                                             // skip a malformed line, keep going
        if (!channel_filter.empty() && e->channel != channel_filter) continue;
        std::printf("%-10lld %-8s %-11s %s <- %s%s%s\n",
                    e->epoch, e->action.c_str(), e->channel.c_str(), e->release.c_str(),
                    e->prev.empty() ? "(none)" : e->prev.c_str(),
                    e->reason.empty() ? "" : "  # ", e->reason.c_str());
    }
    return 0;
}

// Preview parity (strategy metric P2): does the project *as it stands right now* package
// to the exact release a channel currently points at? Answers "is what I'd ship identical
// to what's live?" by comparing package hashes. Exit 0 = parity, 2 = drift (valid inputs,
// but they differ), 1 = error — so a script can tell "drifted" apart from "broke".
int verify_project(const std::string& path, const std::string& channel) {
    if (!engine::valid_channel_name(channel)) {
        std::fprintf(stderr, "verify: invalid channel name '%s'\n", channel.c_str());
        return 1;
    }
    auto r = resolve_project(path);
    if (!r) return 1;
    const std::string local = engine::hash_hex(engine::package_hash(r->resources));
    auto live = read_channel(channel);
    if (!live) {
        std::fprintf(stderr, "verify: channel '%s' is unset or malformed — nothing to compare\n",
                     channel.c_str());
        return 1;
    }
    if (local == *live) {
        std::printf("parity OK: %s == channel %s (%s)\n", path.c_str(), channel.c_str(), local.c_str());
        return 0;
    }
    std::printf("DRIFT: local %s != channel %s %s\n", local.c_str(), channel.c_str(), live->c_str());
    return 2;
}

// The Hub shell (view/controller), headless: one project's aggregate status across the
// project + release domain, ending in the single next recommended action. The view is
// assembled by engine::build_hub_view and rendered via engine::hub_lines — the SAME
// content the graphical Hub Scene (--hub-ui) draws, so CLI and window never drift.
int hub_dashboard(const std::string& path) {
    auto v = engine::build_hub_view(path, kKnownEntries);
    if (!v) { std::fprintf(stderr, "hub: cannot read or parse '%s'\n", path.c_str()); return 1; }
    for (const auto& line : engine::hub_lines(*v)) std::printf("%s\n", line.c_str());
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

    // Headless: scaffold a new launchable game.project manifest (the "create" verb).
    if (mode == "--project-new") {
        if (argc < 4) {
            std::fprintf(stderr, "usage: demo --project-new <out-path> <entry> [name]\n");
            return 1;
        }
        const std::string out = argv[2], entry = argv[3];
        return new_project(out, entry, argc > 4 ? argv[4] : entry);   // default name = entry id
    }

    // Headless: package a project and store it immutably by content hash, pointing a
    // channel (default "development") at it — the Horizon 1 local release store.
    if (mode == "--project-publish") {
        if (argc < 3) { std::fprintf(stderr, "usage: demo --project-publish <path> [channel] [reason]\n"); return 1; }
        return publish_project(argv[2], argc > 3 ? argv[3] : "development", argc > 4 ? argv[4] : "");
    }

    // Headless: move the <to> channel onto the release the <from> channel holds.
    if (mode == "--release-promote") {
        if (argc < 4) { std::fprintf(stderr, "usage: demo --release-promote <from> <to> [reason]\n"); return 1; }
        return promote_release(argv[2], argv[3], argc > 4 ? argv[4] : "");
    }

    // Headless: point <channel> back at an explicit prior release id.
    if (mode == "--release-rollback") {
        if (argc < 4) { std::fprintf(stderr, "usage: demo --release-rollback <channel> <release-id> [reason]\n"); return 1; }
        return rollback_channel(argv[2], argv[3], argc > 4 ? argv[4] : "");
    }

    // Headless: print what each channel points at and whether the release is present.
    if (mode == "--release-status") {
        return release_status();
    }

    // Headless: print the append-only audit history (optionally filtered to one channel).
    if (mode == "--release-log") {
        return release_log(argc > 2 ? argv[2] : "");
    }

    // Headless: preview parity (P2) — does the project package to the release a channel
    // holds? Exit 0 = match, 2 = drift, 1 = error.
    if (mode == "--project-verify") {
        if (argc < 4) { std::fprintf(stderr, "usage: demo --project-verify <path> <channel>\n"); return 1; }
        return verify_project(argv[2], argv[3]);
    }

    // Headless: the Hub shell — one project's aggregate status + next recommended action.
    if (mode == "--hub") {
        if (argc < 3) { std::fprintf(stderr, "usage: demo --hub <path>\n"); return 1; }
        return hub_dashboard(argv[2]);
    }

    // Windowed: the graphical Hub shell — the same view, drawn; Space/1/2 drive the ops.
    if (mode == "--hub-ui") {
        const std::string proj = (argc > 2) ? argv[2] : "projects/creator.gameproject";
        platform::Config cfg;
        cfg.title     = "hand-engine — hub";
        cfg.fb_width  = 760;
        cfg.fb_height = 480;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        return run_window(cfg, std::make_unique<hubui::HubScene>(proj));
    }

    // Windowed: the Studio shell — nav rail (Hub / Learn / About) over the same domain.
    if (mode == "--shell") {
        const std::string proj = (argc > 2) ? argv[2] : "projects/creator.gameproject";
        platform::Config cfg;
        cfg.title     = "hand-engine — studio";
        cfg.fb_width  = 900;
        cfg.fb_height = 560;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        return run_window(cfg, std::make_unique<studioshell::StudioShellScene>(proj));
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
