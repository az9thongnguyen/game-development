// =============================================================================
//  games/studio_shell/scene_workspace.cpp
// =============================================================================
#include "games/studio_shell/scene_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/anim/flipbook.hpp"
#include "engine/commands/registry.hpp"
#include "engine/document/document.hpp"
#include "engine/renderer2d.hpp"
#include "engine/ui/theme.hpp"
#include "games/sandbox/serialize.hpp"

namespace studioshell {

namespace th = ui::theme;
using platform::Key;
using platform::MouseButton;
using sandbox::Archetype;
using sandbox::Body;
using sandbox::Bouncer;
using sandbox::Mover;
using sandbox::Sprite;
using sandbox::Transform2D;

namespace {

const gfx::Color kSwatches[] = {
    gfx::rgb(230, 90, 80),  gfx::rgb(240, 200, 70), gfx::rgb(90, 200, 120),
    gfx::rgb(80, 160, 240), gfx::rgb(200, 120, 230), gfx::rgb(230, 230, 235),
};
constexpr int kSwatchCount = static_cast<int>(sizeof(kSwatches) / sizeof(kSwatches[0]));
constexpr double kAutosaveSeconds = 10.0;

// A scene for a project that declares none, so the workspace opens on something you
// can drag rather than on an explanation. Same bounds as the farm's framebuffer.
const char* const kEmptyScene = "sandbox1\nbounds 640 360\n";

}  // namespace

SceneWorkspace::SceneWorkspace(std::string scene_path) : path_(std::move(scene_path)) {
    Archetype ball;  ball.name = "ball"; ball.round = true; ball.color = gfx::rgb(240, 200, 70);
    ball.w = ball.h = 26; ball.mover = true; ball.vx = 120; ball.vy = 90; ball.bouncer = true;

    Archetype spin;  spin.name = "spinner"; spin.color = gfx::rgb(90, 200, 120);
    spin.w = 40; spin.h = 14; spin.spinner = true; spin.omega = 2.5f;

    Archetype emit;  emit.name = "emitter"; emit.color = gfx::rgb(80, 160, 240);
    emit.w = emit.h = 22;

    Archetype coin;  coin.name = "coin"; coin.round = true; coin.color = gfx::rgb(240, 220, 120);
    coin.w = coin.h = 16; coin.tag = 1;

    Archetype sweep; sweep.name = "sweeper"; sweep.color = gfx::rgb(230, 90, 80);
    sweep.w = 30; sweep.h = 30; sweep.mover = true; sweep.vx = 100; sweep.vy = 60;
    sweep.bouncer = true;

    palette_ = {
        {"Ball",    ball,  Extra::None},
        {"Spinner", spin,  Extra::None},
        {"Emitter", emit,  Extra::Emitter},
        {"Coin",    coin,  Extra::None},
        {"Sweeper", sweep, Extra::Sweeper},
    };
    load();
}

SceneWorkspace::~SceneWorkspace() {
    if (!commands_registered_) return;
    for (const char* id : {"scene.save", "scene.undo", "scene.redo", "scene.reload", "scene.play"})
        cmd::unregister(id);
}

void SceneWorkspace::note(bool ok, std::string msg) {
    message_ = engine::OpResult{ok, std::move(msg)};
}

void SceneWorkspace::install(const std::string& text) {
    world_ = sandbox::from_scene(text);
    // A restored world is built from new entities, so any handle or index into the old
    // one means nothing. Clearing is the honest answer; keeping a number that now
    // points at a different actor is how an undo silently edits the wrong thing.
    sel_ = -1;
    dragging_ = false;
}

void SceneWorkspace::load() {
    if (path_.empty()) {
        problem_ = "this project declares no scene (add:  asset scene <path>)";
        // Still give the workspace a world, so it is an empty canvas rather than a
        // dead panel — you can place actors and Save will create the file.
        install(kEmptyScene);
        return;
    }
    const doc::Opened o = doc::open(path_);
    if (o.state == doc::OpenState::Missing) {
        problem_ = "cannot read " + path_;
        install(kEmptyScene);
        return;
    }
    install(o.content);
    loaded_ = true;
    problem_.clear();
    stack_.clear();
    stack_.mark_saved();
    if (o.state == doc::OpenState::RecoveryOffered) {
        recovery_pending_ = true;
        recovery_text_ = o.recovered;
    }
}

void SceneWorkspace::take_recovery() {
    if (!recovery_pending_) return;
    const std::string saved = sandbox::to_scene(world_);
    const std::string recovered = recovery_text_;
    recovery_pending_ = false;
    recovery_text_.clear();
    // A REAL command, so Cmd+Z undoes the recovery back to the saved file. A recovery
    // you cannot undo is a second way to lose the version you deliberately saved.
    stack_.push_apply(doc::Command{
        "recover autosave",
        [this, recovered] { install(recovered); },
        [this, saved] { install(saved); },
        0});
    note(true, "recovered unsaved changes to " + path_);
}

void SceneWorkspace::dismiss_recovery() {
    // Keep the saved file AND leave the autosave where it is: declining by reflex
    // must not be the thing that destroys the work. A real save clears it.
    recovery_pending_ = false;
    recovery_text_.clear();
    note(true, "kept the saved " + path_);
}

void SceneWorkspace::commit(const std::string& before, std::string label, std::uint64_t merge) {
    const std::string after = sandbox::to_scene(world_);
    if (after == before) return;   // nothing changed: not history
    // push_apply APPLIES, which re-installs the world we are already in. That is
    // harmless — install is idempotent (D21) and the text is identical — except that
    // install clears the selection, so recording an edit would deselect the actor you
    // just edited. Nudge a slider, lose your selection. Undo and redo genuinely build
    // a DIFFERENT world, and there clearing is right; here it is an artefact.
    const int keep = sel_;
    stack_.push_apply(doc::Command{
        std::move(label),
        [this, after] { install(after); },
        [this, before] { install(before); },
        merge});
    sel_ = keep;
}

bool SceneWorkspace::entity_at(int index, ecs::Entity& out) const {
    if (index < 0) return false;
    sandbox::World& w = const_cast<sandbox::World&>(world_);
    int i = 0;
    bool found = false;
    w.reg.view<Transform2D>([&](ecs::Entity e, Transform2D&) {
        if (i++ == index) { out = e; found = true; }
    });
    return found;
}

int SceneWorkspace::index_at(float wx, float wy) const {
    sandbox::World& w = const_cast<sandbox::World&>(world_);
    int i = 0, hit = -1;
    w.reg.view<Transform2D, Body>([&](ecs::Entity, Transform2D& t, Body& b) {
        const float hw = b.w * t.scale * 0.5f, hh = b.h * t.scale * 0.5f;
        if (wx >= t.x - hw && wx <= t.x + hw && wy >= t.y - hh && wy <= t.y + hh)
            hit = i;                      // keep the LAST match: topmost drawn
        ++i;
    });
    return hit;
}

ui::Rect SceneWorkspace::actor_rect(int index) const {
    if (canvas_.w <= 0 || index < 0) return ui::Rect{};
    sandbox::World& w = const_cast<sandbox::World&>(world_);
    int i = 0;
    ui::Rect out{};
    w.reg.view<Transform2D, Body>([&](ecs::Entity, Transform2D& t, Body& b) {
        if (i++ != index) return;
        const int pw = std::max(1, static_cast<int>(b.w * t.scale * view_scale_));
        const int ph = std::max(1, static_cast<int>(b.h * t.scale * view_scale_));
        out = ui::Rect{view_x_ + static_cast<int>(t.x * view_scale_) - pw / 2,
                       view_y_ + static_cast<int>(t.y * view_scale_) - ph / 2, pw, ph};
    });
    return out;
}

void SceneWorkspace::place(int i, float x, float y) {
    const PaletteItem& it = palette_[static_cast<std::size_t>(i)];
    ecs::Entity e = world_.spawn(it.arch, x, y);
    if (it.extra == Extra::Emitter) {
        Archetype pellet;
        pellet.round = true; pellet.color = gfx::rgb(180, 210, 255);
        pellet.w = pellet.h = 8; pellet.mover = true; pellet.vy = 140;
        pellet.lifetime = true; pellet.ttl = 2.5f;
        sandbox::Spawner sp; sp.interval = 0.6f; sp.proto = pellet;
        world_.reg.add<sandbox::Spawner>(e, sp);
    } else if (it.extra == Extra::Sweeper) {
        sandbox::OnOverlap o;
        o.other_tag = 1; o.action = sandbox::Action::DestroyOther;
        world_.reg.add<sandbox::OnOverlap>(e, o);
    }
}

void SceneWorkspace::toggle_play() {
    if (!playing_) {
        play_snapshot_ = sandbox::to_scene(world_);
        playing_ = true;
        sel_ = -1;
        dragging_ = false;
    } else {
        // Stop restores the pre-Play world and is NOT an edit: a simulation you ran
        // and reverted did not change the document, and putting it on the undo stack
        // would make Cmd+Z replay it.
        install(play_snapshot_);
        playing_ = false;
    }
}

void SceneWorkspace::load_textures() {
    if (textures_loaded_) return;
    textures_loaded_ = true;
    // ponytail: probe the Lab's fixed studio_NN naming (0..31); swap for a manifest
    // when the collection outgrows that. Cross-platform (no <filesystem>).
    for (int i = 0; i < 32; ++i) {
        char nm[16];
        std::snprintf(nm, sizeof nm, "studio_%02d", i);
        if (auto img = gfx::load_image(std::string("textures/") + nm + ".hrt")) {
            tex_names_.push_back(nm);
            tex_[nm] = std::move(*img);
        }
    }
    const auto sheet = [&](const std::string& file, const std::string& nm) {
        if (auto img = gfx::load_image(file)) { tex_names_.push_back(nm); tex_[nm] = std::move(*img); }
    };
    sheet("sprites/spin_8.hrt", "spin_8");
    for (int i = 0; i < 8; ++i) {
        char nm[16];
        std::snprintf(nm, sizeof nm, "sheet_%02d", i);
        sheet(std::string("sprites/") + nm + ".hrt", nm);
    }
}

engine::OpResult SceneWorkspace::save() {
    if (path_.empty()) return engine::OpResult{false, "no scene path to save to"};
    if (!doc::save(path_, sandbox::to_scene(world_)))
        return engine::OpResult{false, "could not write " + path_};
    loaded_ = true;
    problem_.clear();
    stack_.mark_saved();
    autosave_timer_ = 0.0;
    return engine::OpResult{true, "saved " + path_};
}

engine::OpResult SceneWorkspace::reload() {
    if (path_.empty()) return engine::OpResult{false, "no scene to reload"};
    playing_ = false;
    load();
    return engine::OpResult{loaded_, loaded_ ? "reloaded " + path_ : problem_};
}

std::optional<engine::OpResult> SceneWorkspace::take_message() {
    auto m = message_;
    message_.reset();
    return m;
}

std::string SceneWorkspace::status() const {
    if (!loaded_ && !problem_.empty()) return problem_;
    std::string s = (path_.empty() ? std::string("(unsaved scene)") : path_) +
                    (dirty() ? "  *  unsaved" : "  saved");
    s += "   " + std::to_string(world_.alive()) + " actor" + (world_.alive() == 1 ? "" : "s");
    if (playing_) s += "   PLAYING";
    return s;
}

const char* SceneWorkspace::hint() const {
    return "Cmd+K commands   Cmd+S save   Cmd+Z undo   Space play/stop   Del removes";
}

void SceneWorkspace::register_commands() {
    const auto did = [](bool ok, const char* yes, const char* no) {
        return engine::OpResult{ok, ok ? yes : no};
    };
    cmd::register_command(cmd::Info{"scene.save", "Scene: save", "Cmd+S", ""},
                          [this](const std::vector<std::string>&) { return save(); });
    cmd::register_command(cmd::Info{"scene.undo", "Scene: undo", "Cmd+Z", ""},
                          [this, did](const std::vector<std::string>&) {
                              return did(stack_.undo(), "undone", "nothing to undo");
                          });
    cmd::register_command(cmd::Info{"scene.redo", "Scene: redo", "Shift+Cmd+Z", ""},
                          [this, did](const std::vector<std::string>&) {
                              return did(stack_.redo(), "redone", "nothing to redo");
                          });
    cmd::register_command(cmd::Info{"scene.reload", "Scene: reload from disk", "", ""},
                          [this](const std::vector<std::string>&) { return reload(); });
    cmd::register_command(cmd::Info{"scene.play", "Scene: play / stop", "Space", ""},
                          [this](const std::vector<std::string>&) {
                              toggle_play();
                              return engine::OpResult{true, playing_ ? "playing" : "stopped"};
                          });
    commands_registered_ = true;
}

void SceneWorkspace::update(double dt, const platform::InputState& in, bool interactive) {
    anim_time_ += dt;

    // The simulation runs whether or not this tab is showing: a running scene that
    // froze when you looked at the map would make Play mean two different things.
    if (playing_) world_.tick(static_cast<float>(dt));

    // ---- buttons resolved during the last draw -----------------------------
    const int want_pal = want_palette_;
    want_palette_ = -2;
    if (want_pal != -2) armed_ = want_pal;

    if (want_play_)   { want_play_ = false; toggle_play(); }
    if (want_undo_)   { want_undo_ = false; if (!stack_.undo()) note(false, "nothing to undo"); }
    if (want_redo_)   { want_redo_ = false; if (!stack_.redo()) note(false, "nothing to redo"); }
    if (want_save_)   { want_save_ = false; message_ = save(); }

    // A property drag (a slider) writes straight into the component every frame and
    // is committed ONCE when the pointer is released, so one drag is one undo step.
    if (editing_prop_ && !in.down(MouseButton::Left)) {
        editing_prop_ = false;
        commit(prop_before_, "adjust actor");
        prop_before_.clear();
    }

    if (!interactive) { dragging_ = false; return; }

    // ---- keyboard ----------------------------------------------------------
    if (!in.mods.super && !in.mods.ctrl) {
        if (in.pressed(Key::Space)) toggle_play();
        if (!playing_ && (in.pressed(Key::Delete) || in.pressed(Key::Backspace)))
            want_delete_ = true;
        if (in.pressed(Key::Escape)) { armed_ = -1; sel_ = -1; }
    }

    // These three change the world, so they run only while this workspace is live —
    // and AFTER the keyboard, so a Delete key pressed this frame is acted on this
    // frame. Reading a key into a flag that the handler above has already passed
    // costs a frame for no reason and reads like a bug the first time you trace it.
    if (want_delete_) {
        want_delete_ = false;
        ecs::Entity e{};
        if (!playing_ && entity_at(sel_, e)) {
            const std::string before = sandbox::to_scene(world_);
            world_.reg.destroy(e);
            sel_ = -1;
            commit(before, "delete actor");
        }
    }
    if (want_recolor_) {
        want_recolor_ = false;
        ecs::Entity e{};
        if (!playing_ && entity_at(sel_, e)) {
            const std::string before = sandbox::to_scene(world_);
            color_idx_ = (color_idx_ + 1) % kSwatchCount;
            if (Sprite* s = world_.reg.get<Sprite>(e)) s->color = kSwatches[color_idx_];
            commit(before, "recolour actor");
        }
    }
    if (want_tex_) {
        want_tex_ = false;
        ecs::Entity e{};
        if (!playing_ && !tex_names_.empty() && entity_at(sel_, e)) {
            if (Sprite* s = world_.reg.get<Sprite>(e)) {
                const std::string before = sandbox::to_scene(world_);
                int idx = -1;
                for (std::size_t k = 0; k < tex_names_.size(); ++k)
                    if (tex_names_[k] == s->texture) { idx = static_cast<int>(k); break; }
                ++idx;
                s->texture = idx >= static_cast<int>(tex_names_.size())
                                 ? std::string()
                                 : tex_names_[static_cast<std::size_t>(idx)];
                auto it = tex_.find(s->texture);
                s->frames = it != tex_.end()
                                ? anim::frames_in_sheet(it->second.w, it->second.h) : 1;
                commit(before, "change texture");
            }
        }
    }


    // ---- canvas pointer ----------------------------------------------------
    // Only inside the canvas rect the last draw reported, and only in world units:
    // the shell's pixels and the scene's coordinates are different spaces, and the
    // conversion lives here so the renderer and the hit test cannot disagree.
    const bool inside = canvas_.w > 0 && in.mouse_x >= canvas_.x && in.mouse_y >= canvas_.y &&
                        in.mouse_x < canvas_.x + canvas_.w && in.mouse_y < canvas_.y + canvas_.h;
    const float wx = (static_cast<float>(in.mouse_x) - static_cast<float>(view_x_)) / view_scale_;
    const float wy = (static_cast<float>(in.mouse_y) - static_cast<float>(view_y_)) / view_scale_;

    if (!playing_ && inside && in.pressed(MouseButton::Left)) {
        if (armed_ >= 0) {
            const std::string before = sandbox::to_scene(world_);
            place(armed_, wx, wy);
            sel_ = index_at(wx, wy);
            commit(before, std::string("place ") + palette_[static_cast<std::size_t>(armed_)].label);
        } else {
            const int hit = index_at(wx, wy);
            sel_ = hit;
            ecs::Entity e{};
            if (hit >= 0 && entity_at(hit, e)) {
                dragging_ = true;
                drag_before_ = sandbox::to_scene(world_);
                if (Transform2D* t = world_.reg.get<Transform2D>(e)) {
                    drag_dx_ = wx - t->x;
                    drag_dy_ = wy - t->y;
                }
            }
        }
    }
    if (dragging_ && in.down(MouseButton::Left)) {
        ecs::Entity e{};
        if (entity_at(sel_, e))
            if (Transform2D* t = world_.reg.get<Transform2D>(e)) {
                t->x = wx - drag_dx_;
                t->y = wy - drag_dy_;
            }
    }
    if (dragging_ && !in.down(MouseButton::Left)) {
        dragging_ = false;
        // ONE undo step for the whole drag, committed from the position it ended at.
        // The snapshot command is idempotent (D21), which is what lets it be recorded
        // after the movement rather than driving it.
        commit(drag_before_, "move actor");
        drag_before_.clear();
    }

    // ---- autosave ----------------------------------------------------------
    if (!path_.empty() && dirty() && !playing_) {
        autosave_timer_ += dt;
        if (autosave_timer_ >= kAutosaveSeconds) {
            autosave_timer_ = 0.0;
            doc::write_autosave(path_, sandbox::to_scene(world_));
        }
    }
}

void SceneWorkspace::draw_canvas(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) {
    (void)ui;
    // First draw: probe the Texture Lab collection. Not in the constructor — it is
    // ~40 speculative file reads, and a test that only exercises editing should not
    // pay for them.
    load_textures();
    canvas_ = area;
    g.fill_round_rect(area.x, area.y, area.w, area.h, th::radius_md, th::elevated);

    const float bw = world_.bounds_w > 0 ? world_.bounds_w : 1.0f;
    const float bh = world_.bounds_h > 0 ? world_.bounds_h : 1.0f;
    // Fit the whole scene: an editor that cropped the world would let you place an
    // actor where you cannot see it, which is the one thing a placement tool must not do.
    const float pad = static_cast<float>(th::space_md);
    view_scale_ = std::min((static_cast<float>(area.w) - pad * 2) / bw,
                           (static_cast<float>(area.h) - pad * 2) / bh);
    if (view_scale_ <= 0.0f) view_scale_ = 0.01f;
    const int dw = static_cast<int>(bw * view_scale_);
    const int dh = static_cast<int>(bh * view_scale_);
    view_x_ = area.x + (area.w - dw) / 2;
    view_y_ = area.y + (area.h - dh) / 2;

    g.push_clip(area.x, area.y, area.w, area.h);
    g.fill_rect(view_x_, view_y_, dw, dh, th::bg);
    g.draw_rect(view_x_, view_y_, dw, dh, th::border_strong);

    const auto sx = [&](float x) { return view_x_ + static_cast<int>(x * view_scale_); };
    const auto sy = [&](float y) { return view_y_ + static_cast<int>(y * view_scale_); };

    int i = 0;
    world_.reg.view<Transform2D, Body, Sprite>(
        [&](ecs::Entity, Transform2D& t, Body& b, Sprite& s) {
            const int cx = sx(t.x), cy = sy(t.y);
            const int w = std::max(1, static_cast<int>(b.w * t.scale * view_scale_));
            const int h = std::max(1, static_cast<int>(b.h * t.scale * view_scale_));
            auto tit = s.texture.empty() ? tex_.end() : tex_.find(s.texture);
            if (tit != tex_.end()) {
                const gfx::Image& img = tit->second;
                gfx::Sprite spr{img.pixels.data(), img.w, img.h};
                if (s.frames > 1 && s.fps > 0 && img.h >= s.frames) {
                    const int fh = img.h / s.frames;
                    const int f = static_cast<int>(anim_time_ * s.fps) % s.frames;
                    spr = {img.pixels.data() + static_cast<std::size_t>(f) * fh * img.w, img.w, fh};
                }
                g.blit_scaled(spr, cx - w / 2, cy - h / 2, w, h);
            } else if (s.round) {
                g.fill_circle(cx, cy, w / 2, s.color);
            } else {
                g.fill_rect(cx - w / 2, cy - h / 2, w, h, s.color);
            }
            // The orientation tick: without it a spinner is a shape that never
            // visibly turns, and "did Play do anything" has no answer.
            const int ex = cx + static_cast<int>(std::cos(t.rot) * w * 0.5f);
            const int ey = cy + static_cast<int>(std::sin(t.rot) * h * 0.5f);
            g.draw_line(cx, cy, ex, ey, th::bg);
            if (i == sel_) g.draw_rect(cx - w / 2 - 2, cy - h / 2 - 2, w + 4, h + 4, th::accent);
            ++i;
        });
    g.pop_clip();
}

void SceneWorkspace::draw_inspector(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) {
    g.fill_round_rect(area.x, area.y, area.w, area.h, th::radius_md, th::elevated);
    ui.push_id("scenei");

    const int x = area.x + th::space_md;
    const int w = area.w - th::space_md * 2;
    int y = area.y + th::space_md;

    g.set_font_size(th::sz_label);
    g.draw_text(x, y, playing_ ? "Playing" : "Scene", playing_ ? th::success : th::text);
    y += th::sz_label + th::space_xs;
    g.set_font_size(th::sz_caption);
    {
        char sub[96];
        std::snprintf(sub, sizeof sub, "%zu actors   %.0f x %.0f",
                      world_.alive(), world_.bounds_w, world_.bounds_h);
        g.draw_text(x, y, sub, th::text_muted);
    }
    y += th::sz_caption + th::space_md;

    if (ui.button(ui::Rect{x, y, w, 30}, playing_ ? "Stop" : "Play", /*primary*/ true))
        want_play_ = true;
    y += 30 + th::space_md;

    if (!playing_) {
        g.set_font_size(th::sz_caption);
        g.draw_text(x, y, "PLACE", th::text_muted);
        y += th::sz_caption + th::space_xs;
        if (ui.button(ui::Rect{x, y, w, 26}, "Select / Move", armed_ < 0)) want_palette_ = -1;
        y += 26 + th::space_xs;
        for (std::size_t k = 0; k < palette_.size(); ++k) {
            ui.push_id(static_cast<int>(k));
            if (ui.button(ui::Rect{x, y, w, 26}, palette_[k].label,
                          armed_ == static_cast<int>(k)))
                want_palette_ = static_cast<int>(k);
            ui.pop_id();
            y += 26 + th::space_xs;
        }
        y += th::space_sm;

        ecs::Entity e{};
        if (entity_at(sel_, e)) {
            g.set_font_size(th::sz_caption);
            g.draw_text(x, y, "SELECTED", th::text_muted);
            y += th::sz_caption + th::space_xs;
            if (Transform2D* t = world_.reg.get<Transform2D>(e)) {
                // A slider edits the component directly; update() commits one undo
                // step when the pointer is released. Capturing the "before" on the
                // FIRST change of a drag is what makes that one step the whole drag.
                if (ui.slider(ui::Rect{x, y, w, 22}, "rot", t->rot, -3.14159f, 3.14159f)) {
                    if (!editing_prop_) { editing_prop_ = true; prop_before_ = sandbox::to_scene(world_); }
                }
                y += 22 + th::space_xs;
                if (ui.slider(ui::Rect{x, y, w, 22}, "scale", t->scale, 0.3f, 3.0f)) {
                    if (!editing_prop_) { editing_prop_ = true; prop_before_ = sandbox::to_scene(world_); }
                }
                y += 22 + th::space_sm;
            }
            bool bounce = world_.reg.has<Bouncer>(e);
            if (ui.checkbox(ui::Rect{x, y, w, 22}, "bounces", bounce)) {
                const std::string before = sandbox::to_scene(world_);
                if (bounce) world_.reg.add<Bouncer>(e, {});
                else        world_.reg.remove<Bouncer>(e);
                commit(before, "toggle bounce");
            }
            y += 22 + th::space_xs;
            bool moves = world_.reg.has<Mover>(e);
            if (ui.checkbox(ui::Rect{x, y, w, 22}, "moves", moves)) {
                const std::string before = sandbox::to_scene(world_);
                if (moves) world_.reg.add<Mover>(e, {90, 60});
                else       world_.reg.remove<Mover>(e);
                commit(before, "toggle mover");
            }
            y += 22 + th::space_sm;

            if (ui.button(ui::Rect{x, y, w / 2 - 2, 26}, "Recolour")) want_recolor_ = true;
            if (ui.button(ui::Rect{x + w / 2 + 2, y, w / 2 - 2, 26}, "Delete")) want_delete_ = true;
            y += 26 + th::space_xs;

            if (Sprite* s = world_.reg.get<Sprite>(e)) {
                char lbl[48];
                std::snprintf(lbl, sizeof lbl, "Texture: %s",
                              s->texture.empty() ? "none" : s->texture.c_str());
                if (ui.button(ui::Rect{x, y, w, 26}, lbl, false, !tex_names_.empty()))
                    want_tex_ = true;
                y += 26 + th::space_xs;
                if (tex_names_.empty()) {
                    g.set_font_size(th::sz_caption);
                    g.draw_text(x, y, "(make textures in --studio)", th::text_muted);
                    y += th::sz_caption + th::space_xs;
                }
            }
        } else {
            g.set_font_size(th::sz_caption);
            g.draw_text(x, y, armed_ >= 0 ? "click the canvas to place"
                                          : "click an actor to select it", th::text_muted);
            y += th::sz_caption + th::space_sm;
        }
    }

    // ---- history, pinned to the bottom -------------------------------------
    // Undo/Redo/Save sit at the far end so they do not move when the inspector above
    // them grows and shrinks with the selection.
    {
        int by = area.y + area.h - th::space_md - 30 - th::space_xs - 26 - th::space_xs - th::sz_caption;
        const int half = w / 2 - 2;
        if (ui.button(ui::Rect{x, by, half, 26}, "Undo", false, stack_.can_undo())) want_undo_ = true;
        if (ui.button(ui::Rect{x + w / 2 + 2, by, half, 26}, "Redo", false, stack_.can_redo()))
            want_redo_ = true;
        by += 26 + th::space_xs;
        if (ui.button(ui::Rect{x, by, w, 30}, "Save", false, !path_.empty())) want_save_ = true;
        by += 30 + th::space_xs;
        g.set_font_size(th::sz_caption);
        const std::string lbl = stack_.can_undo() ? "undo: " + stack_.undo_label()
                                                  : std::string("nothing to undo");
        g.draw_text(x, by, lbl.c_str(), th::text_muted);
    }

    ui.pop_id();
}

} // namespace studioshell
