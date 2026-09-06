// =============================================================================
//  games/studio_shell/scene_workspace.cpp
// =============================================================================
#include "games/studio_shell/scene_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "engine/anim/flipbook.hpp"
#include "engine/commands/registry.hpp"
#include "engine/fx/light.hpp"
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
    // Flipbooks advance whether or not the scene is running — an editor showing a
    // frozen frame cannot tell you what the animation looks like. ONE clock, on the
    // sprite, so the stopped preview and the playing frame are the same number.
    world_.animate(static_cast<float>(dt));

    // The simulation runs whether or not this tab is showing: a running scene that
    // froze when you looked at the map would make Play mean two different things.
    if (playing_) {
        world_.tick(static_cast<float>(dt));
        // The model recorded what should be heard; carry it to whoever owns a device.
        for (const sandbox::Sound& s : world_.sounds)
            pending_sounds_.push_back(SoundRequest{s.freq, s.ms, s.gain});
    }

    // ---- buttons resolved during the last draw -----------------------------
    const int want_pal = want_palette_;
    want_palette_ = -2;
    if (want_pal != -2) armed_ = want_pal;

    if (want_play_)   { want_play_ = false; toggle_play(); }
    if (want_undo_)   { want_undo_ = false; if (!stack_.undo()) note(false, "nothing to undo"); }
    if (want_redo_)   { want_redo_ = false; if (!stack_.redo()) note(false, "nothing to redo"); }
    if (want_save_)   { want_save_ = false; message_ = save(); }
    if (want_audition_) {
        want_audition_ = false;
        ecs::Entity e{};
        if (entity_at(sel_, e))
            if (const sandbox::Sound* s = world_.reg.get<sandbox::Sound>(e))
                pending_sounds_.push_back(SoundRequest{s->freq, s->ms, s->gain});
    }
    if (want_restart_) {
        want_restart_ = false;
        ecs::Entity e{};
        if (entity_at(sel_, e))
            if (Sprite* s = world_.reg.get<Sprite>(e)) s->t = 0.0f;   // clock, not an edit
    }
    if (want_light_color_) {
        want_light_color_ = false;
        ecs::Entity e{};
        if (entity_at(sel_, e))
            if (sandbox::Light* L = world_.reg.get<sandbox::Light>(e)) {
                const std::string before = sandbox::to_scene(world_);
                int at = 0;
                for (int k = 0; k < kSwatchCount; ++k)
                    if (kSwatches[k] == L->color) { at = k + 1; break; }
                L->color = kSwatches[at % kSwatchCount];
                commit(before, "light colour");
            }
    }

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
                    const int f = sandbox::sprite_frame(s);
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

    // Effects are clipped to the WORLD, not to the panel. A 160 px light on an actor
    // near the edge of a 320 px scene otherwise bleeds across the editor background,
    // which reads as a rendering fault and — worse — hides where the world stops.
    // Only the screenshot showed this; every assertion about it was green.
    g.push_clip(view_x_, view_y_, dw, dh);

    // ---- lights: additive, over the actors, inside the canvas clip ---------
    // The light lab's deposit loop, unchanged except that the centre now comes from a
    // Transform2D — which is the whole point of making it a component. ponytail: naive
    // O(radius^2) per light, same as the lab; a radial sprite blit if counts ever grow.
    world_.reg.view<sandbox::Light, Transform2D>(
        [&](ecs::Entity, sandbox::Light& L, Transform2D& t) {
            const float r = L.radius * view_scale_;
            if (r <= 0.0f || L.intensity <= 0.0f) return;
            const fx::Light lit{static_cast<float>(sx(t.x)), static_cast<float>(sy(t.y)),
                                r, L.color, L.intensity};
            const int x0 = std::max(view_x_, static_cast<int>(lit.x - r));
            const int y0 = std::max(view_y_, static_cast<int>(lit.y - r));
            const int x1 = std::min(view_x_ + dw, static_cast<int>(lit.x + r) + 1);
            const int y1 = std::min(view_y_ + dh, static_cast<int>(lit.y + r) + 1);
            for (int py = y0; py < y1; ++py)
                for (int px = x0; px < x1; ++px) {
                    const gfx::Color c = fx::light_sample(lit, static_cast<float>(px),
                                                          static_cast<float>(py));
                    if (gfx::a_of(c)) g.add_pixel(px, py, c);
                }
        });

    // ---- particles ---------------------------------------------------------
    world_.reg.view<sandbox::Emitter>([&](ecs::Entity, sandbox::Emitter& em) {
        for (const fx::Particle& p : em.sys.particles())
            g.fill_circle(sx(p.x), sy(p.y),
                          std::max(1, static_cast<int>(fx::current_size(p) * view_scale_)),
                          fx::current_color(p));
    });

    // ---- the selected emitter's aim, drawn while STOPPED -------------------
    // Without this an emitter is configured blind: the particles only fly during Play,
    // so `dir` and `spread` would be two sliders with nothing on screen answering them.
    {
        ecs::Entity e{};
        if (entity_at(sel_, e))
            if (sandbox::Emitter* em = world_.reg.get<sandbox::Emitter>(e))
                if (Transform2D* t = world_.reg.get<Transform2D>(e)) {
                    const float len = std::max(12.0f, em->cfg.speed * 0.35f) * view_scale_;
                    const int   cx = sx(t->x), cy = sy(t->y);
                    for (float a : {em->cfg.dir - em->cfg.spread, em->cfg.dir,
                                    em->cfg.dir + em->cfg.spread})
                        g.draw_line(cx, cy, cx + static_cast<int>(std::cos(a) * len),
                                    cy + static_cast<int>(std::sin(a) * len), th::accent);
                }
    }
    g.pop_clip();   // the world
    g.pop_clip();   // the canvas
}

void SceneWorkspace::mark(const char* id, ui::Rect r) {
    // A control is only "at" a rect if a finger could land there. Outside the scrolling
    // viewport it is drawn nowhere and clickable nowhere, and a test that asked for it
    // must be told that, not handed coordinates that look usable.
    const ui::Rect v = viewport_;
    const int x0 = std::max(r.x, v.x), y0 = std::max(r.y, v.y);
    const int x1 = std::min(r.x + r.w, v.x + v.w), y1 = std::min(r.y + r.h, v.y + v.h);
    controls_[id] = (x1 > x0 && y1 > y0) ? ui::Rect{x0, y0, x1 - x0, y1 - y0} : ui::Rect{};
}

ui::Rect SceneWorkspace::control_rect(const char* id) const {
    const auto it = controls_.find(id);
    return it == controls_.end() ? ui::Rect{} : it->second;
}

std::vector<studioshell::Workspace::SoundRequest> SceneWorkspace::take_sounds() {
    std::vector<SoundRequest> out;
    out.swap(pending_sounds_);
    return out;
}

void SceneWorkspace::draw_inspector(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) {
    g.fill_round_rect(area.x, area.y, area.w, area.h, th::radius_md, th::elevated);
    ui.push_id("scenei");
    controls_.clear();

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

    // ---- the footer's geometry, decided BEFORE the body is drawn -----------
    // Undo/Redo/Save are pinned to the bottom so they do not move as the selection
    // grows and shrinks. The body between them and the header therefore has a fixed
    // budget, and effect components blow through it — which is why the body scrolls
    // rather than growing until it draws over the buttons (chapters 127 and 132: a
    // control drawn outside its panel is a control that cannot be pressed).
    const int foot_h = 26 + th::space_xs + 30 + th::space_xs + th::sz_caption + th::space_md;
    const int foot_y = area.y + area.h - foot_h;

    viewport_ = ui::Rect{area.x, y, area.w, std::max(0, foot_y - y)};
    const ui::Rect vp = ui.begin_scroll("scenebody", viewport_, content_h_);
    const int      top = vp.y;
    int            cy  = vp.y;

    // ui::Context::slider draws its "label: value" ABOVE the rect it is given — the
    // rect is the groove, not the control — so a row must reserve that line too.
    // Advancing by the groove alone stacks every label on top of whatever is above it,
    // which is exactly what the panel looked like the first time it was screenshotted.
    const auto slider_row = [&](int& cur) {
        cur += th::sz_caption + 2;
        const ui::Rect r{x, cur, w, 20};
        cur += 20 + th::space_xs;
        return r;
    };

    if (!playing_) {
        g.set_font_size(th::sz_caption);
        g.draw_text(x, cy, "PLACE", th::text_muted);
        cy += th::sz_caption + th::space_xs;
        if (ui.button(ui::Rect{x, cy, w, 26}, "Select / Move", armed_ < 0)) want_palette_ = -1;
        cy += 26 + th::space_xs;
        for (std::size_t k = 0; k < palette_.size(); ++k) {
            ui.push_id(static_cast<int>(k));
            if (ui.button(ui::Rect{x, cy, w, 26}, palette_[k].label,
                          armed_ == static_cast<int>(k)))
                want_palette_ = static_cast<int>(k);
            ui.pop_id();
            cy += 26 + th::space_xs;
        }
        cy += th::space_sm;

        ecs::Entity e{};
        if (entity_at(sel_, e)) {
            g.set_font_size(th::sz_caption);
            g.draw_text(x, cy, "SELECTED", th::text_muted);
            cy += th::sz_caption + th::space_xs;
            if (Transform2D* t = world_.reg.get<Transform2D>(e)) {
                // A slider edits the component directly; update() commits one undo
                // step when the pointer is released. Capturing the "before" on the
                // FIRST change of a drag is what makes that one step the whole drag.
                if (ui.slider(slider_row(cy), "rot", t->rot, -3.14159f, 3.14159f)) {
                    if (!editing_prop_) { editing_prop_ = true; prop_before_ = sandbox::to_scene(world_); }
                }
                if (ui.slider(slider_row(cy), "scale", t->scale, 0.3f, 3.0f)) {
                    if (!editing_prop_) { editing_prop_ = true; prop_before_ = sandbox::to_scene(world_); }
                }
                cy += th::space_sm;
            }
            bool bounce = world_.reg.has<Bouncer>(e);
            if (ui.checkbox(ui::Rect{x, cy, w, 22}, "bounces", bounce)) {
                const std::string before = sandbox::to_scene(world_);
                if (bounce) world_.reg.add<Bouncer>(e, {});
                else        world_.reg.remove<Bouncer>(e);
                commit(before, "toggle bounce");
            }
            cy += 22 + th::space_xs;
            bool moves = world_.reg.has<Mover>(e);
            if (ui.checkbox(ui::Rect{x, cy, w, 22}, "moves", moves)) {
                const std::string before = sandbox::to_scene(world_);
                if (moves) world_.reg.add<Mover>(e, {90, 60});
                else       world_.reg.remove<Mover>(e);
                commit(before, "toggle mover");
            }
            cy += 22 + th::space_sm;

            if (ui.button(ui::Rect{x, cy, w / 2 - 2, 26}, "Recolour")) want_recolor_ = true;
            if (ui.button(ui::Rect{x + w / 2 + 2, cy, w / 2 - 2, 26}, "Delete")) want_delete_ = true;
            cy += 26 + th::space_xs;

            if (Sprite* s = world_.reg.get<Sprite>(e)) {
                char lbl[48];
                std::snprintf(lbl, sizeof lbl, "Texture: %s",
                              s->texture.empty() ? "none" : s->texture.c_str());
                if (ui.button(ui::Rect{x, cy, w, 26}, lbl, false, !tex_names_.empty()))
                    want_tex_ = true;
                cy += 26 + th::space_xs;
                if (tex_names_.empty()) {
                    g.set_font_size(th::sz_caption);
                    g.draw_text(x, cy, "(make textures in --studio)", th::text_muted);
                    cy += th::sz_caption + th::space_xs;
                }
            }

            // ---- EFFECTS: the four labs, on this actor ---------------------
            cy += th::space_sm;
            g.set_font_size(th::sz_caption);
            g.draw_text(x, cy, "EFFECTS", th::text_muted);
            cy += th::sz_caption + th::space_xs;

            // Add or drop a component as ONE undo step. Same shape as the bounce and
            // mover checkboxes above it, which is the point: an effect is not a
            // special kind of thing, it is another component on the same actor.
            const auto toggle = [&](bool want, const char* label, auto made) {
                using T = decltype(made);
                const std::string before = sandbox::to_scene(world_);
                if (want) world_.reg.add<T>(e, made);
                else      world_.reg.remove<T>(e);
                commit(before, label);
            };
            // A slider that edits a live component: the same deferred-commit dance the
            // transform sliders do, so one drag is one undo step.
            const auto touched = [&](bool changed) {
                if (changed && !editing_prop_) {
                    editing_prop_ = true;
                    prop_before_  = sandbox::to_scene(world_);
                }
            };

            bool has_em = world_.reg.has<sandbox::Emitter>(e);
            mark("emitter", ui::Rect{x, cy, w, 22});
            if (ui.checkbox(ui::Rect{x, cy, w, 22}, "emitter", has_em))
                toggle(has_em, "toggle emitter", sandbox::Emitter{});
            cy += 22 + th::space_xs;
            if (sandbox::Emitter* em = world_.reg.get<sandbox::Emitter>(e)) {
                const ui::Rect rate_r = slider_row(cy);
                mark("emitter.rate", rate_r);
                touched(ui.slider(rate_r, "rate", em->cfg.rate, 0.0f, 700.0f));
                touched(ui.slider(slider_row(cy), "speed", em->cfg.speed, 0.0f, 400.0f));
                touched(ui.slider(slider_row(cy), "spread", em->cfg.spread, 0.0f, 3.14159f));
                touched(ui.slider(slider_row(cy), "gravity", em->cfg.gravity, -200.0f, 700.0f));
                touched(ui.slider(slider_row(cy), "dir", em->cfg.dir, -3.14159f, 3.14159f));
                cy += th::space_sm;
            }

            bool has_li = world_.reg.has<sandbox::Light>(e);
            mark("light", ui::Rect{x, cy, w, 22});
            if (ui.checkbox(ui::Rect{x, cy, w, 22}, "light", has_li))
                toggle(has_li, "toggle light", sandbox::Light{});
            cy += 22 + th::space_xs;
            if (sandbox::Light* L = world_.reg.get<sandbox::Light>(e)) {
                touched(ui.slider(slider_row(cy), "radius", L->radius, 20.0f, 400.0f));
                touched(ui.slider(slider_row(cy), "glow", L->intensity, 0.0f, 3.0f));
                mark("light.colour", ui::Rect{x, cy, w, 26});
                if (ui.button(ui::Rect{x, cy, w, 26}, "Light colour")) want_light_color_ = true;
                cy += 26 + th::space_sm;
            }

            bool has_sd = world_.reg.has<sandbox::Sound>(e);
            mark("sound", ui::Rect{x, cy, w, 22});
            if (ui.checkbox(ui::Rect{x, cy, w, 22}, "sound", has_sd))
                toggle(has_sd, "toggle sound", sandbox::Sound{});
            cy += 22 + th::space_xs;
            if (sandbox::Sound* sd = world_.reg.get<sandbox::Sound>(e)) {
                touched(ui.slider(slider_row(cy), "pitch", sd->freq, 110.0f, 880.0f));
                touched(ui.slider(slider_row(cy), "gain", sd->gain, 0.0f, 1.0f));
                mark("sound.audition", ui::Rect{x, cy, w, 26});
                if (ui.button(ui::Rect{x, cy, w, 26}, "Audition")) want_audition_ = true;
                cy += 26 + th::space_xs;
                g.set_font_size(th::sz_caption);
                g.draw_text(x, cy, "heard when this actor is destroyed", th::text_muted);
                cy += th::sz_caption + th::space_sm;
            }

            // ---- FLIPBOOK: only an animated sprite has one to show ----------
            if (Sprite* s = world_.reg.get<Sprite>(e)) {
                if (s->frames > 1) {
                    g.set_font_size(th::sz_caption);
                    g.draw_text(x, cy, "FLIPBOOK", th::text_muted);
                    cy += th::sz_caption + th::space_xs;
                    bool loop = s->loop;
                    mark("flipbook.loop", ui::Rect{x, cy, w, 22});
                    if (ui.checkbox(ui::Rect{x, cy, w, 22}, "loop", loop)) {
                        const std::string before = sandbox::to_scene(world_);
                        s->loop = loop;
                        commit(before, "toggle loop");
                    }
                    cy += 22 + th::space_xs;
                    touched(ui.slider(slider_row(cy), "fps", s->fps, 1.0f, 24.0f));
                    mark("flipbook.restart", ui::Rect{x, cy, w, 26});
                    if (ui.button(ui::Rect{x, cy, w, 26}, "Restart")) want_restart_ = true;
                    cy += 26 + th::space_xs;
                    char fr[48];
                    std::snprintf(fr, sizeof fr, "frame %d / %d%s",
                                  sandbox::sprite_frame(*s) + 1, s->frames,
                                  anim::Flipbook{s->frames, s->fps, s->loop, s->t}.done()
                                      ? "  (done)" : "");
                    g.set_font_size(th::sz_caption);
                    g.draw_text(x, cy, fr, th::text_muted);
                    cy += th::sz_caption + th::space_sm;
                }
            }
        } else {
            g.set_font_size(th::sz_caption);
            g.draw_text(x, cy, armed_ >= 0 ? "click the canvas to place"
                                           : "click an actor to select it", th::text_muted);
            cy += th::sz_caption + th::space_sm;
        }
    } else {
        // Editing is off while the scene runs, and an empty panel does not say so.
        g.set_font_size(th::sz_caption);
        g.draw_text(x, cy, "running — Stop to edit", th::text_muted);
        cy += th::sz_caption + th::space_sm;
    }
    // What the body actually needed, fed back to begin_scroll NEXT frame: immediate
    // mode places before it can measure, so last frame's height is the only honest
    // number there is. It is stable because the layout only changes when the user does.
    content_h_ = cy - top + th::space_md;
    ui.end_scroll();

    // ---- history, pinned to the bottom -------------------------------------
    {
        int by = foot_y;
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
