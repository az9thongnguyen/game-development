// =============================================================================
//  main.cpp  —  engine entry point + mode dispatch
// =============================================================================
//  Usage:
//    demo                 -> M0 engine demo (retro 480x270 window)
//    demo --gui [hvh|hvai] [easy|medium|hard]  -> chess (large, crisp window)
//    demo --tui [hvh|hvai] [easy|medium|hard]  -> chess in the terminal
// =============================================================================
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
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
#include "engine/project/inspect.hpp"
#include "engine/project/project.hpp"
#include "engine/release/ops.hpp"
#include "engine/commands/registry.hpp"
#include "engine/commands/all_commands.hpp"
#include "engine/release/release.hpp"
#include "engine/resource/resource.hpp"
#include "engine/text/font.hpp"
#include "platform/platform.hpp"

#include "demo/demo_scene.hpp"
#include "games/chess/chess_gui.hpp"
#include "games/chess/chess_tui.hpp"
#include "games/farm/farm_scene.hpp"
#include "games/fps/raycast_scene.hpp"
#include "games/viz3d/scene3d.hpp"
#include "games/viz3d/editor_scene.hpp"
#include "games/iso/iso_scene.hpp"
#include "games/editor/editor_scene.hpp"
#include "games/colony/colony_scene.hpp"
#include "games/studio/studio_scene.hpp"
#include "games/anim/anim_scene.hpp"
#include "games/audio/audio_scene.hpp"
#include "games/fx/fx_scene.hpp"
#include "games/light/light_scene.hpp"
#include "games/studio_shell/map_workspace.hpp"
#include "games/studio_shell/pixel_workspace.hpp"
#include "games/studio_shell/scene_workspace.hpp"
#include "games/studio_shell/studio_shell_scene.hpp"
#include "games/studio_shell/workspace_host.hpp"

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
    // The Studio's speaker, wired here because this is the layer allowed to know about
    // SDL. Everything above studio_shell/sound_bank.hpp only asks for a sound.
    studioshell::set_audio_device({&platform::init_audio, &platform::audio_rate,
                                   &platform::play_sound});

    engine::App app(std::move(scene));
    platform::run([&app](double dt) { app.frame(dt); });

    platform::shutdown();
    return 0;
}

// Launch a reference-game entry scene by its manifest id. Single source of truth
// shared by the --fps flag and the --project path, so a project manifest selects a
// game without editing this dispatch. Extend the table when a
// new entry scene is added. ponytail: starts with the one Horizon 0 reference game.
// The ONE entry table. It used to be two hand-synced things: a chain of `if`s in
// launch_entry, and a kKnownEntries literal beside it with a comment asking whoever
// edited one to remember the other. Chapter 114 is about what happens to lists that
// are written down twice, so this slice does not add a third for the Play viewport —
// launching, validating and playing all read this.
struct Entry {
    std::string      id;
    platform::Config cfg;                                       // window + native size
    std::function<std::unique_ptr<engine::Scene>()> make;       // a fresh scene
};

const std::vector<Entry>& entries() {
    static const std::vector<Entry> table = [] {
        std::vector<Entry> v;
        {
            platform::Config c;
            c.title = "hand-engine — fps";
            c.fb_width = 640; c.fb_height = 400;
            c.scale = 1; c.smooth = true; c.highdpi = true;
            v.push_back({"fps", c, [] {
                return std::unique_ptr<engine::Scene>(new fps::RaycastScene());
            }});
        }
        {
            platform::Config c;
            c.title = "hand-engine — farm";
            c.fb_width = 640; c.fb_height = 360;
            c.scale = 2; c.smooth = false;   // a tile game wants crisp pixels
            c.highdpi = true; c.supersample = kAA;
            v.push_back({"farm", c, [] {
                return std::unique_ptr<engine::Scene>(new farm::FarmScene());
            }});
        }
        return v;
    }();
    return table;
}

const Entry* find_entry(const std::string& id) {
    for (const Entry& e : entries())
        if (e.id == id) return &e;
    return nullptr;
}

int launch_entry(const std::string& entry) {
    const Entry* e = find_entry(entry);
    if (!e) { std::fprintf(stderr, "unknown entry scene: %s\n", entry.c_str()); return 1; }
    return run_window(e->cfg, e->make());
}

// ---- labs: everything windowed that is NOT a game with a manifest ---------------
//
// Twelve of these used to be twelve top-level flags, each a copy of the same eight
// lines of platform::Config. That is what "29 CLI modes" was made of, and it made the
// surface of this project look like its inventory rather than its product.
//
// A LAB is a demo of an engine subsystem, or a scene that has not earned a manifest
// yet. A GAME is declared by a manifest and launched with --project; the two tables
// are separate on purpose, so `entry fx` in a manifest stays impossible.
//
// Same shape as entries(), for the same reason: one table, and every trigger derives
// from it — `--lab` with no id lists exactly what `--lab <id>` can run.
const std::vector<Entry>& labs() {
    static const std::vector<Entry> table = [] {
        auto win = [](const char* title, int w, int h, int ss) {
            platform::Config c;
            c.title = title;
            c.fb_width = w; c.fb_height = h;
            c.scale = 1; c.smooth = true; c.highdpi = true;
            c.supersample = ss;
            return c;
        };
        std::vector<Entry> v;
        v.push_back({"scene", win("hand-engine — scene workspace", 960, 600, kAA), [] {
            // The Studio's Scene tab, full-screen. The SAME object, not a second
            // editor — that is what chapter 116 absorbed, and this is the frame.
            return std::unique_ptr<engine::Scene>(new studioshell::WorkspaceHost(
                std::make_unique<studioshell::SceneWorkspace>("scenes/demo.scene")));
        }});
        v.push_back({"pixel", win("hand-engine — pixel editor", 960, 600, kAA), [] {
            // The Studio's Pixels tab, full-screen — the SAME object, like `scene`.
            // Both textures the farm declares, so the editable one is reachable
            // without editing the imported pack.
            return std::unique_ptr<engine::Scene>(new studioshell::WorkspaceHost(
                std::make_unique<studioshell::PixelWorkspace>(
                    std::vector<std::string>{"textures/farm_water.hrt", "textures/town.hrt"},
                    "projects/farm.gameproject")));
        }});
        v.push_back({"map", win("hand-engine — map editor", 960, 600, kAA), [] {
            // The SAME object as the Studio's Map tab, full-screen — like `scene` and
            // `pixel`. The door keeps its name; what is behind it changed. Chapter 132
            // retired the separate Map Lab that used to be here, which wrote the older
            // fpsmap1 and was the only place a spawn could be authored.
            return std::unique_ptr<engine::Scene>(new studioshell::WorkspaceHost(
                std::make_unique<studioshell::MapWorkspace>("maps/level_00.map2")));
        }});
        v.push_back({"texture", win("hand-engine — texture lab", 960, 600, kAA), [] {
            return std::unique_ptr<engine::Scene>(new studio::StudioScene());
        }});
        v.push_back({"editor", win("hand-engine — editor (UI + physics)", 960, 600, kAA), [] {
            return std::unique_ptr<engine::Scene>(new editor::EditorScene());
        }});
        v.push_back({"fx", win("hand-engine — particle fx", 960, 600, kAA), [] {
            return std::unique_ptr<engine::Scene>(new fx::FxScene());
        }});
        // Lights are soft and per-pixel additive; SSAA would quadruple the fill for
        // an edge that is not there.
        v.push_back({"light", win("hand-engine — 2D lighting", 960, 600, 1), [] {
            return std::unique_ptr<engine::Scene>(new lightdemo::LightScene());
        }});
        v.push_back({"audio", win("hand-engine — audio mixer", 960, 600, kAA), [] {
            return std::unique_ptr<engine::Scene>(new audiodemo::AudioScene());
        }});
        v.push_back({"anim", win("hand-engine — sprite animation", 960, 600, kAA), [] {
            return std::unique_ptr<engine::Scene>(new animdemo::AnimScene());
        }});
        v.push_back({"3d", win("hand-engine — 3D core", 960, 600, kAA), [] {
            return std::unique_ptr<engine::Scene>(new viz3d::Scene3D());
        }});
        v.push_back({"viz3d", win("hand-engine — viz3d sandbox", 960, 600, kAA), [] {
            return std::unique_ptr<engine::Scene>(new viz3d::EditorScene());
        }});
        v.push_back({"iso", win("hand-engine — iso farm sim", 960, 600, kAA), [] {
            return std::unique_ptr<engine::Scene>(new iso::IsoScene());
        }});
        {   // the colony wants room for the taller design-system panel
            platform::Config c = win("hand-engine — colony (ECS + jobs + UI)", 1000, 760, kAA);
            v.push_back({"colony", c, [] {
                return std::unique_ptr<engine::Scene>(new colony::ColonyScene());
            }});
        }
        return v;
    }();
    return table;
}

// Every flag this build answers to, and — just as important — where the retired ones
// went. Someone with muscle memory for `--maplab` should be told, not silently handed
// a different window.
int usage(const std::string& unknown) {
    if (!unknown.empty()) std::fprintf(stderr, "unknown mode: %s\n\n", unknown.c_str());
    std::fprintf(stderr,
        "usage: demo [mode] [args]\n"
        "\n  games\n"
        "    --project <manifest>            launch a game from its game.project\n"
        "    --gui | --tui [hvh|hvai] [easy|medium|hard]   chess\n"
        "\n  authoring\n"
        "    --shell [manifest]              the Studio\n"
        "    --lab [id]                      a demo or an unshipped scene (no id lists them)\n"
        "\n  the platform spine (headless)\n"
        "    --project-new <path> <entry> <name>\n"
        "    --project-inspect <manifest>    --project-package <manifest>\n"
        "    --project-publish <manifest> <channel> <reason>\n"
        "    --project-verify  <manifest> <channel>\n"
        "    --release-promote <from> <to> <reason>\n"
        "    --release-rollback <channel> <release-id> <reason>\n"
        "    --release-status                --release-log [channel]\n"
        "    --hub <manifest>                --cmd [id] [args...]\n"
        "    --bench-ui [frames] [manifest]  --runner <base_url> <api_key>\n"
        "\n  retired (chapter 120)\n"
        "    --hub-ui   -> --shell, Hub section\n"
        "    --sandbox  -> --lab scene        --editor -> --lab editor\n"
        "    --studio   -> --lab texture\n"
        "\n  retired (chapter 132)\n"
        "    --maplab -> --lab map, which is now the Studio's Map workspace. The old\n"
        "               lab wrote fpsmap1; convert a level with --cmd map.migrate\n"
        "    --3d --viz3d --iso --colony --fx --light --audio --anim -> --lab <same name>\n"
        "\n  no mode at all runs the M0 engine demo.\n");
    return unknown.empty() ? 0 : 2;
}

const std::vector<std::string>& known_entries();   // defined just below run_lab

// `--lab` with no id lists what there is, the same way `--cmd` with no id does: a
// door you cannot see through is a door nobody opens.
int run_lab(const std::string& id) {
    if (id.empty()) {
        std::printf("labs (demos and scenes with no manifest):\n");
        for (const Entry& e : labs()) std::printf("  %-8s %s\n", e.id.c_str(), e.cfg.title);
        std::printf("\na game with a manifest is launched with: demo --project <path>\n");
        return 0;
    }
    for (const Entry& e : labs())
        if (e.id == id) {
            // The same registry the Studio fills before building its scene. A lab is
            // the SAME workspace object as a tab (ch. 120), so an operation it can
            // perform in one frame and not the other is the drift that rule exists to
            // prevent — and `--lab pixel` creating no new sheet because nobody called
            // this is exactly that, silently.
            cmd::register_all(known_entries());
            return run_window(e.cfg, e.make());
        }
    std::fprintf(stderr, "unknown lab: %s   (run `demo --lab` to list them)\n", id.c_str());
    return 1;
}

// DERIVED from the table, so a game cannot be launchable-but-unknown (the manifest
// refuses it) or known-but-unlaunchable (validate passes, then the launch fails).
const std::vector<std::string>& known_entries() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> v;
        for (const Entry& e : entries()) v.push_back(e.id);
        return v;
    }();
    return ids;
}

// Strict load: an Inspection that is actually shippable, or nullopt after printing
// why. Callers that need a shippable project (launch, package) go through here; the
// inspect report below wants the partial answer, so it reads the Inspection directly.
std::optional<engine::Inspection> resolve_project(const std::string& path) {
    engine::Inspection in = engine::inspect(path, known_entries());
    if (in.shippable()) return in;
    // A manifest that cannot be read or parsed has one problem and it is fatal; a
    // manifest that parsed has a list, and printing all of it is the difference
    // between one debugging run and one per broken asset.
    if (!in.parsed) {
        std::fprintf(stderr, "project: %s\n", in.problems.front().c_str());
        return std::nullopt;
    }
    std::fprintf(stderr, "project: '%s' is not shippable:\n", path.c_str());
    for (const auto& e : in.problems) std::fprintf(stderr, "  - %s\n", e.c_str());
    return std::nullopt;
}

// The inspection report. The flag is an ALIAS onto the registry command, not a second
// formatter beside it (D16): --project-inspect, --cmd project.inspect and a Studio
// button print the identical text because there is only one that produces it. The
// report goes to stdout either way — "not shippable, here is why" is this verb's
// ANSWER, not a malfunction, and a caller redirecting stderr must still get it.
int inspect_project(const std::string& path) {
    cmd::register_all(known_entries());
    const engine::OpResult r = cmd::run("project.inspect", {path});
    std::printf("%s\n", r.message.c_str());
    return r.ok ? 0 : 1;
}

// Launch a game.project manifest's entry scene (strict: refuses a broken closure).
int launch_project(const std::string& path) {
    auto r = resolve_project(path);
    if (!r) return 1;
    return launch_entry(r->project.entry);
}

// Emit the deterministic package manifest (identity + content-hashed resources +
// combined package hash) for a project to stdout. Refuses a project that would not launch.
int package_project(const std::string& path) {
    auto r = resolve_project(path);
    if (!r) return 1;
    std::printf("%s", engine::build_package(r->project.name, r->project.schema,
                                            r->project.entry, r->resources()).c_str());
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
    const auto errs = engine::validate(p, known_entries());   // known entry + non-empty name, fail-closed
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
// The mutating verbs are ALIASES onto the command registry, not a second path to the
// same operation. Two call sites that happen to agree today is how a GUI and a CLI
// drift apart; going through cmd::run means --project-publish, --cmd project.publish
// and a Studio button are provably the same code, and adding a command cannot add it
// to only some of them.
int run_command(const char* id, const std::vector<std::string>& args) {
    cmd::register_all(known_entries());
    return print_op(cmd::run(id, args));
}
int publish_project(const std::string& path, const std::string& channel, const std::string& reason) {
    return run_command("project.publish", {path, channel, reason});
}
int promote_release(const std::string& from, const std::string& to, const std::string& reason) {
    return run_command("release.promote", {from, to, reason});
}
int rollback_channel(const std::string& channel, const std::string& hex, const std::string& reason) {
    return run_command("release.rollback", {channel, hex, reason});
}

// Status and log now come from release_ops_core as DATA; main.cpp only formats them.
// They used to be implemented here, printing directly, which meant a window showing the
// same information had to reimplement the reading — the duplication hub_lines exists to
// prevent one level up.
int release_status() {
    for (const auto& c : engine::status()) {
        if (c.release.empty()) { std::printf("%-11s unset\n", c.name.c_str()); continue; }
        std::printf("%-11s %s  [%s]\n", c.name.c_str(), c.release.c_str(),
                    c.present ? "present" : "MISSING");
    }
    return 0;
}

int release_log(const std::string& channel_filter) {
    const auto records = engine::log(channel_filter);
    if (records.empty()) { std::printf("(no releases published yet)\n"); return 0; }
    for (const auto& e : records)
        std::printf("%-10lld %-8s %-11s %s <- %s%s%s\n",
                    e.epoch, e.action.c_str(), e.channel.c_str(), e.release.c_str(),
                    e.prev.empty() ? "(none)" : e.prev.c_str(),
                    e.reason.empty() ? "" : "  # ", e.reason.c_str());
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
    const std::string local = r->package;   // inspect() already computed it
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
    auto v = engine::build_hub_view(path, known_entries());
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
        return launch_project(argv[2]);
    }

    // Headless: validate a manifest and print an inspection report, no window.
    if (mode == "--project-inspect") {
        if (argc < 3) { std::fprintf(stderr, "usage: demo --project-inspect <path>\n"); return 1; }
        return inspect_project(argv[2]);
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
    // Run any registered command by id. This is the seam that makes "a GUI action and
    // a CLI verb are the same code" structural rather than a convention: the Studio's
    // buttons and this flag both go through cmd::run.
    //
    // The older flags below are kept and are now one-line aliases onto the same
    // handlers, so CI and every script keep working unchanged.
    if (mode == "--cmd") {
        cmd::register_all(known_entries());
        if (argc < 3) {
            std::printf("usage: --cmd <id> [args...]\n\nregistered commands:\n");
            for (const auto& i : cmd::all())
                std::printf("  %-18s %s%s%s\n", i.id.c_str(), i.title.c_str(),
                            i.args_help.empty() ? "" : "   ", i.args_help.c_str());
            return 1;
        }
        std::vector<std::string> args;
        for (int i = 3; i < argc; ++i) args.emplace_back(argv[i]);
        const engine::OpResult r = cmd::run(argv[2], args);
        if (r.ok) { if (!r.message.empty()) std::printf("%s\n", r.message.c_str()); return 0; }
        std::fprintf(stderr, "%s\n", r.message.c_str());
        return 1;
    }

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

    // Headless: how long does one Studio frame take to rasterize?
    //
    // The Studio draws into a CPU framebuffer, so every pixel costs real work and
    // supersampling costs four times as much. Before the UI layer grows, measure it —
    // a budget you never measured is a budget you are not keeping. No window is
    // involved: a Scene needs only an engine::Context, so this runs in CI too.
    if (mode == "--bench-ui") {
        const int frames = (argc > 2) ? std::atoi(argv[2]) : 120;
        auto bytes = assets::load_file("fonts/Inter.ttf");
        if (!bytes) { std::fprintf(stderr, "bench-ui: cannot load fonts/Inter.ttf\n"); return 1; }
        auto font = text::Font::load_from_bytes(std::move(*bytes));
        if (!font) { std::fprintf(stderr, "bench-ui: cannot parse Inter.ttf\n"); return 1; }

        const std::string proj = (argc > 3) ? argv[3] : "projects/creator.gameproject";
        constexpr int LW = 1280, LH = 720;
        // Which build this is matters more than any other line of output: the same
        // code is ~5x slower unoptimized, so a Debug number quoted as a shipping
        // cost is simply wrong.
#ifdef NDEBUG
        const char* build = "Release";
#else
        const char* build = "Debug (unoptimized — NOT the shipping cost)";
#endif
        std::printf("bench-ui  %dx%d logical, %d frames per configuration (after 20 warm-up)\n",
                    LW, LH, frames);
        std::printf("          build: %s\n", build);

        for (int ss : {1, 2}) {
            studioshell::StudioShellScene scene(proj, known_entries());
            std::vector<std::uint32_t> buf(static_cast<std::size_t>(LW * ss) * (LH * ss), 0);
            platform::Framebuffer fb{buf.data(), LW * ss, LH * ss, LW * ss};
            platform::InputState  in{};

            // Warm up before timing. The first frames pay for first-touch page faults
            // on a freshly-allocated 14 MB framebuffer and for rasterizing each type-scale
            // size once; including them measures start-up, not steady state, and produces
            // a number that drifts by 3x between runs.
            {
                gfx::Renderer2D r(fb, ss);
                const engine::Context ctx{r, in, 1.0 / 60.0, 0.0, 0.0, font.get()};
                for (int i = 0; i < 20; ++i) scene.render(ctx);
            }

            std::vector<double> ms;
            ms.reserve(static_cast<std::size_t>(frames));
            for (int i = 0; i < frames; ++i) {
                const auto t0 = std::chrono::steady_clock::now();
                gfx::Renderer2D r(fb, ss);
                const engine::Context ctx{r, in, 1.0 / 60.0, 0.0, 0.0, font.get()};
                scene.render(ctx);
                const auto t1 = std::chrono::steady_clock::now();
                ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
            std::sort(ms.begin(), ms.end());
            const double med = ms[ms.size() / 2];
            const double p95 = ms[static_cast<std::size_t>(ms.size() * 95 / 100)];
            std::printf("  ss=%d  (%d x %d physical px)   median %6.2f ms   p95 %6.2f ms   %s\n",
                        ss, LW * ss, LH * ss, med, p95,
                        med <= 8.0 ? "within the 8 ms budget" : "OVER the 8 ms budget");
        }
        return 0;
    }

    // Windowed: the Studio shell — nav rail (Hub / Guide / Learn / About) over the same domain.
    if (mode == "--help" || mode == "-h") return usage("");

    // One door for every windowed thing that is not a game: `--lab` lists, `--lab <id>`
    // runs. This replaced twelve flags that each carried their own copy of the same
    // window config.
    if (mode == "--lab") return run_lab(argc > 2 ? argv[2] : "");

    if (mode == "--shell") {
        const std::string proj = (argc > 2) ? argv[2] : "projects/creator.gameproject";
        platform::Config cfg;
        cfg.title     = "hand-engine — studio";
        cfg.fb_width  = 1280;    // the Studio is a workspace, not a retro scene: it needs room
        cfg.fb_height = 720;
        cfg.scale     = 1;
        cfg.smooth    = true;
        cfg.highdpi   = true;
        cfg.supersample = kAA;   // scenes without it, so their text was upscaled, not rasterized
        cfg.resizable = true;    // a workspace should fit the screen it is on
        cfg.quit_on_escape = false;   // Escape now closes a dialog; quitting on it loses work
        // The release ops go into the registry BEFORE the scene, so the Studio's
        // command palette lists everything this process can do — not only what the
        // workspace itself registers. Same registry `--cmd` reads; what differs is
        // what a given process has registered, which is the honest difference.
        cmd::register_all(known_entries());
        auto scene = std::make_unique<studioshell::StudioShellScene>(proj, known_entries());
        scene->set_clipboard(&platform::clipboard_get, &platform::clipboard_set);
                // The Play viewport builds its scene from the SAME entry table --project
        // launches from, so a game that plays in the Studio is the game that runs.
        scene->set_play_factory([](const std::string& id) {
            studioshell::PlayTarget t;
            if (const Entry* e = find_entry(id)) {
                t.scene = e->make();
                t.w = e->cfg.fb_width;
                t.h = e->cfg.fb_height;
            }
            return t;
        });
        return run_window(cfg, std::move(scene));
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

    // An unknown flag used to fall through to the M0 demo — so a typo, or a flag that
    // has been retired, opened the wrong window and said nothing. A CLI that answers a
    // question you did not ask is worse than one that refuses.
    if (mode.rfind("--", 0) == 0) return usage(mode);

    // No args: the M0 engine demo (retro 480x270, nearest scaling).
    platform::Config cfg;
    cfg.title     = "hand-engine — M0";
    cfg.fb_width  = 480;
    cfg.fb_height = 270;
    cfg.scale     = 2;   // retro: nearest scaling, no SSAA (keep the chunky M0 look)
    return run_window(cfg, std::make_unique<demo::DemoScene>());
}
