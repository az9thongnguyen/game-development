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
// =============================================================================
#pragma once

namespace platform {

enum class Key {
    Unknown = 0,
    Up, Down, Left, Right,
    W, A, S, D,
    Space, Enter, Escape,
    // Added for the M3.5 editor (the table in backend_sdl.cpp maps each to a
    // scancode; new entries "just work" because the pump loops over that table).
    Num1, Num2, Num3, Num4,
    Q, E, R, F, G, C, X,
    Minus, Equals,
    Tab, Delete, Backspace,
    // Added for the M4 iso sim: brushes 5..0 and save/load function keys.
    Num5, Num6, Num7, Num8, Num9, Num0,
    F5, F9,
    Count
};

enum class MouseButton { Left = 0, Right, Middle, Count };

struct InputState {
    bool key_down[static_cast<int>(Key::Count)]     = {};
    bool key_pressed[static_cast<int>(Key::Count)]  = {};
    bool key_released[static_cast<int>(Key::Count)] = {};

    // Mouse position is in FRAMEBUFFER (logical) coordinates, so it lines up with
    // what the renderer draws regardless of the window's integer scale factor.
    int  mouse_x = 0, mouse_y = 0;
    bool mouse_down[static_cast<int>(MouseButton::Count)]     = {};
    bool mouse_pressed[static_cast<int>(MouseButton::Count)]  = {};
    bool mouse_released[static_cast<int>(MouseButton::Count)] = {};

    bool down(Key k)     const { return key_down[static_cast<int>(k)]; }
    bool pressed(Key k)  const { return key_pressed[static_cast<int>(k)]; }
    bool released(Key k) const { return key_released[static_cast<int>(k)]; }

    bool down(MouseButton b)     const { return mouse_down[static_cast<int>(b)]; }
    bool pressed(MouseButton b)  const { return mouse_pressed[static_cast<int>(b)]; }
    bool released(MouseButton b) const { return mouse_released[static_cast<int>(b)]; }
};

} // namespace platform
