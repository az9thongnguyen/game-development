// =============================================================================
//  platform/input.hpp  —  normalized input snapshot (part of the platform seam)
// =============================================================================
//  The platform PRODUCES this snapshot from raw SDL events each frame; the engine
//  and games CONSUME it (via Context). It deliberately lives in the platform
//  layer — it is "normalized hardware state" — so the one-directional dependency
//  rule (game → engine → platform) holds and no SDL type ever leaks upward.
//
//  Three states per key/button:
//    down     — currently held this frame
//    pressed  — transitioned up→down THIS frame (an edge; good for "fire once")
//    released — transitioned down→up THIS frame
//
//  No STL here on purpose: fixed arrays only, so this header stays cheap to include
//  from anything and the snapshot is trivially copyable.
// =============================================================================
#pragma once

#include <cstddef>

namespace platform {

enum class Key {
    Unknown = 0,
    Up, Down, Left, Right,
    Space, Enter, Escape,
    // The whole alphabet. Shortcuts need arbitrary letters (Ctrl+K, Ctrl+Z, Ctrl+S),
    // and adding them one at a time means editing two files per paper cut. Typed
    // TEXT does not come through here — it arrives decoded in `text` below.
    A, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Minus, Equals,
    Tab, Delete, Backspace,
    Home, End, PageUp, PageDown,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Count
};

enum class MouseButton { Left = 0, Right, Middle, Count };

// Modifier state, sampled once per frame. Modifiers are not Keys: nothing wants to
// know that Shift was "pressed this frame", only whether it is held while something
// else happens.
struct Mods {
    bool shift = false, ctrl = false, alt = false, super = false;
};

struct InputState {
    bool key_down[static_cast<int>(Key::Count)]     = {};
    bool key_pressed[static_cast<int>(Key::Count)]  = {};
    bool key_released[static_cast<int>(Key::Count)] = {};
    // The OS auto-repeat fired for this key this frame. Held Backspace should delete
    // more than one character; held Space should not fire an action more than once.
    // Keeping repeat separate from `pressed` lets each caller choose.
    bool key_repeat[static_cast<int>(Key::Count)]   = {};

    Mods mods{};

    // Mouse position is in FRAMEBUFFER (logical) coordinates, so it lines up with
    // what the renderer draws regardless of the window's integer scale factor.
    int  mouse_x = 0, mouse_y = 0;
    bool mouse_down[static_cast<int>(MouseButton::Count)]     = {};
    bool mouse_pressed[static_cast<int>(MouseButton::Count)]  = {};
    bool mouse_released[static_cast<int>(MouseButton::Count)] = {};

    // Wheel ticks accumulated this frame (+y = away from the user, +x = right).
    int wheel_x = 0, wheel_y = 0;

    // Text COMMITTED this frame, UTF-8, not NUL-terminated. This is what the OS
    // decided the keystrokes mean — dead keys, compose sequences, IME candidates and
    // non-US layouts all resolve here, which is exactly why a text field must read
    // this and never try to reconstruct characters from Key edges.
    static constexpr std::size_t kTextMax = 32;
    char        text[kTextMax] = {};
    std::size_t text_len = 0;

    // The window changed size this frame; win_w/win_h are its new LOGICAL size.
    bool window_resized = false;
    int  win_w = 0, win_h = 0;

    bool down(Key k)     const { return key_down[static_cast<int>(k)]; }
    bool pressed(Key k)  const { return key_pressed[static_cast<int>(k)]; }
    bool released(Key k) const { return key_released[static_cast<int>(k)]; }
    // "the user asked for this again" — a fresh press OR an auto-repeat.
    bool repeated(Key k) const { return key_pressed[static_cast<int>(k)] ||
                                        key_repeat[static_cast<int>(k)]; }

    // The "command" modifier — Cmd on a Mac, Ctrl everywhere else — and it accepts
    // EITHER, on every platform, on purpose. A compile-time `#ifdef __APPLE__` cannot
    // be right for the web build: that binary is compiled once and run on every OS, so
    // a Mac user in a browser presses Cmd and a Windows user presses Ctrl and the same
    // wasm has to serve both. (Before chapter 123 this did not matter, because the web
    // build received no keys at all.) The cost of accepting both is that Ctrl+S also
    // saves on a Mac, which nobody has ever complained about.
    bool accel() const { return mods.super || mods.ctrl; }

    bool down(MouseButton b)     const { return mouse_down[static_cast<int>(b)]; }
    bool pressed(MouseButton b)  const { return mouse_pressed[static_cast<int>(b)]; }
    bool released(MouseButton b) const { return mouse_released[static_cast<int>(b)]; }
};

} // namespace platform
