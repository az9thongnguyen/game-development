// =============================================================================
//  games/studio_shell/workspace.hpp  —  what the Studio can hold open
// =============================================================================
//  Chapter 112 deliberately did NOT write this. There was one workspace, and an
//  abstract base with a single implementation is a shape moulded around its only
//  occupant: every accidental detail of MapWorkspace would have become part of the
//  contract, and the second implementation would have had to pretend to be a map.
//
//  The second one exists now, so the shape can be drawn from two examples instead of
//  one. What that changed, concretely:
//
//    * status()/hint() are on the WORKSPACE. The shell used to build the map's status
//      line itself — "path, dirty marker, tile under the cursor". A scene has no
//      tiles, so the shell had to stop knowing what it was drawing.
//    * inspector_width() is a request, not a constant. 260 suits a tile palette; an
//      actor inspector with sliders wants more.
//    * recovery is OPTIONAL and defaulted. Not every document autosaves, and forcing
//      a workspace that does not to say so three times is how an interface starts
//      lying about what it means.
//
//  No SDL anywhere below this line: a workspace reads a platform::InputState (a plain
//  struct) and draws through Renderer2D, which is what lets the whole Studio run in
//  the headless golden test.
// =============================================================================
#pragma once

#include <optional>
#include <string>

#include "engine/release/ops.hpp"    // engine::OpResult
#include "engine/ui/ui.hpp"
#include "platform/input.hpp"

namespace gfx { class Renderer2D; }

namespace studioshell {

class Workspace {
public:
    virtual ~Workspace() = default;

    // The tab label. Short: it goes in a tab row, not a title bar.
    [[nodiscard]] virtual const char* name() const = 0;

    // Is there a document, what is it called, and — when there is not — why not.
    // `problem()` is the difference between an empty canvas and an explained one.
    [[nodiscard]] virtual bool               loaded() const = 0;
    [[nodiscard]] virtual const std::string& path() const = 0;
    [[nodiscard]] virtual const std::string& problem() const = 0;
    [[nodiscard]] virtual bool               dirty() const = 0;

    // Bind this object's operations to command ids so the palette lists them. The
    // implementation's destructor must unregister them: a handler that captured
    // `this` and outlives it is a dangling call the palette would happily make (D24).
    virtual void register_commands() = 0;

    // `interactive` is false while a modal is up or another workspace is showing: the
    // workspace may still draw, but a click behind a dialog must not edit anything.
    virtual void update(double dt, const platform::InputState& in, bool interactive) = 0;
    virtual void draw_canvas(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) = 0;
    virtual void draw_inspector(ui::Context& ui, gfx::Renderer2D& g, ui::Rect area) = 0;

    // How wide this workspace would like its inspector. The shell decides in the end
    // (a narrow window overrides it), which is why this is a request and not a rule.
    [[nodiscard]] virtual int inspector_width() const { return 260; }

    // The two halves of the status strip: what is open on the left, what the keys do
    // on the right. Owned here because the shell must not know which document it has.
    [[nodiscard]] virtual std::string status() const = 0;
    [[nodiscard]] virtual const char* hint() const = 0;

    virtual engine::OpResult save() = 0;
    virtual engine::OpResult reload() = 0;

    // The last thing that happened, for the shell's toast. Cleared when read.
    virtual std::optional<engine::OpResult> take_message() = 0;

    // Autosave recovery, if this document has any. Defaulted to "nothing to recover"
    // rather than pure, so a workspace without autosave stays silent instead of
    // implementing three empty methods to say it has nothing to say.
    [[nodiscard]] virtual bool recovery_pending() const { return false; }
    virtual void take_recovery() {}
    virtual void dismiss_recovery() {}
};

} // namespace studioshell
