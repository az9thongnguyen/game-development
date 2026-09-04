// =============================================================================
//  games/studio_shell/play_viewport.hpp  —  a game running inside the Studio
// =============================================================================
//  The scene gets its OWN framebuffer at the game's native size, exactly as the
//  window would give it one, and the panel blits the result. That is the whole
//  design decision: the alternative — letting the scene draw into the Studio's
//  framebuffer under a clip — would make every coordinate a lie, because a scene
//  asks the renderer how big the screen is and centres things in it. A game must not
//  be able to tell it is embedded.
//
//  Why this is worth having at all, over just launching the game in its own window:
//  Pause and Step-one-frame. Watching a single fixed step at a time is the thing you
//  cannot do by running the real thing, and it is the reason an embedded player earns
//  its complexity.
//
//  The scene FACTORY is injected. This file must stay linkable without SDL — that is
//  what lets the golden test drive the whole shell headless — and main.cpp is where
//  the entry table lives.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/fixed_step.hpp"
#include "engine/release/ops.hpp"   // engine::OpResult
#include "engine/scene.hpp"
#include "engine/ui/ui.hpp"

namespace text { class Font; }

namespace studioshell {

// What an entry id resolves to: a scene, and the framebuffer size that game is drawn
// at. Both come from one table in main.cpp, so the viewport cannot run a game at a
// size the real window would not use.
struct PlayTarget {
    std::unique_ptr<engine::Scene> scene;
    int w = 0, h = 0;
};

class PlayViewport {
public:
    using Factory = std::function<PlayTarget(const std::string& entry)>;

    void set_factory(Factory f) { factory_ = std::move(f); }
    [[nodiscard]] bool has_factory() const { return static_cast<bool>(factory_); }
    [[nodiscard]] bool running() const { return scene_ != nullptr; }
    [[nodiscard]] bool paused() const { return paused_; }

    // Build the scene for `entry` and start its clock at zero.
    engine::OpResult start(const std::string& entry);
    void stop();
    void set_paused(bool p) { paused_ = p; }
    // Advance exactly one fixed step on the next update, even while paused.
    void step_once() { pending_step_ = true; }

    // One fixed logic step (or none, while paused). `focused` gates input: a game
    // that ate Cmd+K would make the Studio's own commands unreachable.
    void update(double dt, const platform::InputState& in, bool focused);

    // Render the scene into its own buffer, then blit it into `area` letterboxed at a
    // whole-number scale. Returns the rect the game actually occupies.
    ui::Rect draw(gfx::Renderer2D& g, ui::Rect area, text::Font* font, double real_dt);

    [[nodiscard]] double    clock()  const { return clock_.time(); }
    [[nodiscard]] long long steps()  const { return steps_; }
    [[nodiscard]] int       width()  const { return w_; }
    [[nodiscard]] int       height() const { return h_; }
    [[nodiscard]] const std::string& entry() const { return entry_; }

private:
    Factory                        factory_;
    std::unique_ptr<engine::Scene> scene_;
    std::string                    entry_;
    std::vector<std::uint32_t>     pixels_;
    int                            w_ = 0, h_ = 0;
    engine::FixedStep              clock_;
    bool                           paused_ = false;
    bool                           pending_step_ = false;
    long long                      steps_ = 0;
};

} // namespace studioshell
