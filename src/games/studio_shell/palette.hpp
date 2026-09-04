// =============================================================================
//  games/studio_shell/palette.hpp  —  Cmd+K over the command registry
// =============================================================================
//  The registry's face. Every operation registers once (`cmd::register_command`) and
//  is then reachable three ways that cannot drift apart: `--cmd <id>` on a terminal,
//  a button wired to `cmd::run`, and this list. Adding an operation cannot
//  accidentally add it to only one of them.
//
//  What it deliberately does NOT do: collect arguments. A command with `args_help`
//  is listed (so you can see it exists and what it wants) but running it needs a
//  place to type three separate values with validation, which is the Hub tab's
//  confirm dialog, not a one-line box. Selecting one reports what it needs.
// =============================================================================
#pragma once

#include <string>

#include "engine/ui/ui.hpp"

namespace gfx { class Renderer2D; }

namespace studioshell {

class CommandPalette {
public:
    [[nodiscard]] bool is_open() const { return open_; }
    void open();
    void close();

    // Keyboard, from the scene (which is the only thing that sees the platform).
    void move(int delta);              // Up/Down through the filtered list
    [[nodiscard]] std::string selected() const;   // the id, or empty

    // Draws the scrim, the card, the query field and the rows. Returns the id the
    // user chose by CLICKING a row this frame, or empty. Enter is the scene's, since
    // only it reads the keyboard.
    std::string draw(ui::Context& ui, gfx::Renderer2D& g);

private:
    bool        open_ = false;
    bool        just_opened_ = false;
    std::string query_;
    int         sel_ = 0;
};

} // namespace studioshell
