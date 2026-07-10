// =============================================================================
//  platform/platform.hpp  —  the platform seam (FIXED interface)
// =============================================================================
//  This is the ONE interface that the engine and games are allowed to depend on
//  to reach the outside world. Everything below it (SDL today, Emscripten at M5)
//  is an interchangeable "backend". The rule that makes the eventual web port
//  painless:
//
//      engine + game code  --->  platform.hpp   (never #include <SDL.h>)
//                                     |
//                          backend_sdl.cpp / backend_web.cpp
//
//  Keep this header free of SDL types. If you ever feel the urge to expose an
//  SDL_* type here, that's a sign the abstraction is leaking.
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>

#include "platform/input.hpp"

namespace platform {

// A view onto the CPU pixel buffer we draw into. Pixels are 32-bit ARGB8888,
// laid out row-major. `pitch` is the number of pixels per row (== width for our
// contiguous buffer; kept explicit so renderer code never assumes it).
struct Framebuffer {
    uint32_t* pixels;
    int       width;
    int       height;
    int       pitch;   // pixels per row
};

// How to start the platform. `fb_width`/`fb_height` is the LOGICAL resolution we
// render at; the window is that size times `scale` (crisp integer upscaling).
struct Config {
    const char* title     = "hand-engine";
    int         fb_width  = 480;
    int         fb_height = 270;
    int         scale     = 2;
    bool        smooth    = false;  // present scaling: false=nearest (retro), true=linear (smooth)
    bool        highdpi   = true;   // use the display's full resolution for a crisp present
    int         supersample = 1;    // SSAA: render the framebuffer at NxN and downsample on present
                                    // (1=off). Costs N^2 fill; the game still uses LOGICAL coords.
};

// ---- Lifetime ----
bool init(const Config& cfg);   // returns false on failure (message on stderr)
void shutdown();

// ---- Framebuffer ----
Framebuffer framebuffer();      // the buffer to draw into this frame (PHYSICAL size = logical*supersample)
int         supersample();      // the active SSAA factor (1 if off); Renderer2D scales by this
void        present();          // upload + show it (run() calls this for you)

// ---- Main loop ----
// Calls `frame(dt)` once per frame, dt = seconds since the previous frame, until
// a quit is requested. The loop MECHANISM lives in the backend so the web backend
// (M5) can replace this blocking while-loop with emscripten_set_main_loop without
// any change above the platform layer. This is why engine/game code never writes
// its own `while(true)`.
void run(const std::function<void(double dt)>& frame);

// ---- Input ----
// Normalized keyboard/mouse snapshot, refreshed once per frame before frame().
const InputState& input();

// ---- Audio ----
// Open the audio device (returns false if unavailable). Audio is signed 16-bit
// mono at audio_rate() Hz. play_sound queues a one-shot clip (no-op if audio
// failed to open), so game code never touches SDL audio directly.
bool init_audio();
int  audio_rate();
void play_sound(const int16_t* samples, int count);

// ---- Quit control ----
bool should_quit();
void request_quit();

} // namespace platform
