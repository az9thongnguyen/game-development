// =============================================================================
//  engine/ui/ui_input.hpp  —  platform input → UI intents
// =============================================================================
//  The one place that knows which keys mean what. Widgets ask for "paste", not for
//  Ctrl+V — which is the difference between working on macOS and not, and between a
//  widget that can be unit-tested without SDL and one that cannot.
//
//  Separate header because ui.hpp itself must stay platform-free; only the scene
//  that already talks to the platform includes this.
// =============================================================================
#pragma once

#include "engine/ui/ui.hpp"
#include "platform/platform.hpp"

namespace ui {

inline Input from_platform(const platform::InputState& p) {
    using K = platform::Key;
    Input in;
    in.mx       = p.mouse_x;
    in.my       = p.mouse_y;
    in.down     = p.down(platform::MouseButton::Left);
    in.pressed  = p.pressed(platform::MouseButton::Left);
    in.released = p.released(platform::MouseButton::Left);
    in.wheel    = p.wheel_y;
    in.text     = p.text;
    in.text_len = p.text_len;

    // The command modifier. One definition, on the input struct — it used to be an
    // `#ifdef __APPLE__` here and in three other files, which is four chances to
    // disagree and, on the web, four chances to be wrong at once.
    const bool cmd = p.accel();

    Keys& k = in.keys;
    // Shift+Tab is "focus backwards", so the two are mutually exclusive.
    k.tab        = p.repeated(K::Tab) && !p.mods.shift;
    k.tab_back   = p.repeated(K::Tab) &&  p.mods.shift;
    k.activate   = p.pressed(K::Enter) || p.pressed(K::Space);
    k.cancel     = p.pressed(K::Escape);
    // Navigation and editing repeat: holding Backspace should delete more than one
    // character, and holding Left should walk the caret.
    k.left       = p.repeated(K::Left);
    k.right      = p.repeated(K::Right);
    k.up         = p.repeated(K::Up);
    k.down       = p.repeated(K::Down);
    k.home       = p.pressed(K::Home);
    k.end        = p.pressed(K::End);
    k.backspace  = p.repeated(K::Backspace);
    k.del        = p.repeated(K::Delete);
    k.copy       = cmd && p.pressed(K::C);
    k.cut        = cmd && p.pressed(K::X);
    k.paste      = cmd && p.pressed(K::V);
    k.select_all = cmd && p.pressed(K::A);
    k.shift      = p.mods.shift;

    // A chord is a command, not a character. Without this, Cmd+A would also fire
    // "activate", and any chord that the OS happens to commit as text would be typed
    // into whatever field has focus.
    if (cmd) { k.activate = false; in.text = nullptr; in.text_len = 0; }
    return in;
}

} // namespace ui
