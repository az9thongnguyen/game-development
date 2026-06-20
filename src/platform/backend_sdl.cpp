// =============================================================================
//  platform/backend_sdl.cpp  —  the SDL2 implementation of platform.hpp
// =============================================================================
//  The ONLY file allowed to #include <SDL.h>. SDL is used as a thin shim:
//  window, a framebuffer we present via one texture blit, raw input, time. No SDL
//  drawing primitives. Input is read by POLLING SDL's key/mouse state each frame
//  (robust to event-delivery quirks under sdl2-compat / HiDPI); mouse position is
//  mapped from window points to framebuffer coords by simple ratio.
// =============================================================================
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "platform/platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace platform {
namespace {

SDL_Window*           g_window   = nullptr;
SDL_Renderer*         g_renderer = nullptr;  // used ONLY to present the texture
SDL_Texture*          g_texture  = nullptr;  // streaming texture we upload into
std::vector<uint32_t> g_pixels;              // <-- THE framebuffer (ARGB8888)
int                   g_fb_w = 0;
int                   g_fb_h = 0;
bool                  g_quit  = false;
InputState            g_input;

long g_max_frames = -1;  // HAND_ENGINE_FRAMES test hook (-1 = run until quit)

void pump_events() {
    // Pump the OS queue (also updates SDL's internal input state) + watch quit.
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_quit = true;
    }

    // ---- Keyboard: poll state, edge-detect against last frame ----
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    struct KeyMap { Key k; SDL_Scancode sc; };
    static const KeyMap kmap[] = {
        {Key::Up, SDL_SCANCODE_UP}, {Key::Down, SDL_SCANCODE_DOWN},
        {Key::Left, SDL_SCANCODE_LEFT}, {Key::Right, SDL_SCANCODE_RIGHT},
        {Key::W, SDL_SCANCODE_W}, {Key::A, SDL_SCANCODE_A},
        {Key::S, SDL_SCANCODE_S}, {Key::D, SDL_SCANCODE_D},
        {Key::Space, SDL_SCANCODE_SPACE}, {Key::Enter, SDL_SCANCODE_RETURN},
        {Key::Escape, SDL_SCANCODE_ESCAPE},
    };
    for (const KeyMap& m : kmap) {
        const bool now = ks[m.sc] != 0;
        const bool was = g_input.key_down[int(m.k)];
        g_input.key_pressed[int(m.k)]  = now && !was;
        g_input.key_released[int(m.k)] = !now && was;
        g_input.key_down[int(m.k)]     = now;
    }
    if (g_input.key_down[int(Key::Escape)]) g_quit = true;

    // ---- Mouse: poll position + buttons; map window points -> framebuffer ----
    int mx = 0, my = 0;
    const Uint32 mask = SDL_GetMouseState(&mx, &my);
    int ww = g_fb_w, wh = g_fb_h;
    SDL_GetWindowSize(g_window, &ww, &wh);
    g_input.mouse_x = (ww > 0) ? mx * g_fb_w / ww : mx;
    g_input.mouse_y = (wh > 0) ? my * g_fb_h / wh : my;
    struct BtnMap { MouseButton b; Uint32 m; };
    static const BtnMap bmap[] = {
        {MouseButton::Left,   SDL_BUTTON_LMASK},
        {MouseButton::Right,  SDL_BUTTON_RMASK},
        {MouseButton::Middle, SDL_BUTTON_MMASK},
    };
    for (const BtnMap& bm : bmap) {
        const bool now = (mask & bm.m) != 0;
        const bool was = g_input.mouse_down[int(bm.b)];
        g_input.mouse_pressed[int(bm.b)]  = now && !was;
        g_input.mouse_released[int(bm.b)] = !now && was;
        g_input.mouse_down[int(bm.b)]     = now;
    }
}

} // anonymous namespace

bool init(const Config& cfg) {
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "platform: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    g_fb_w = cfg.fb_width;
    g_fb_h = cfg.fb_height;
    const int win_w = cfg.fb_width  * cfg.scale;
    const int win_h = cfg.fb_height * cfg.scale;

    const Uint32 win_flags = SDL_WINDOW_SHOWN |
                             (cfg.highdpi ? SDL_WINDOW_ALLOW_HIGHDPI : 0u);
    g_window = SDL_CreateWindow(cfg.title,
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                win_w, win_h, win_flags);
    if (!g_window) {
        std::fprintf(stderr, "platform: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    g_renderer = SDL_CreateRenderer(g_window, -1,
                                    SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer) {
        std::fprintf(stderr, "platform: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }

    // Smooth (linear) present for real artwork; nearest for crisp retro pixels.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, cfg.smooth ? "linear" : "nearest");
    SDL_RenderSetLogicalSize(g_renderer, g_fb_w, g_fb_h);
    SDL_RenderSetIntegerScale(g_renderer, cfg.smooth ? SDL_FALSE : SDL_TRUE);

    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, g_fb_w, g_fb_h);
    if (!g_texture) {
        std::fprintf(stderr, "platform: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    g_pixels.assign(static_cast<size_t>(g_fb_w) * static_cast<size_t>(g_fb_h), 0xFF000000u);

    if (const char* s = std::getenv("HAND_ENGINE_FRAMES")) {
        g_max_frames = std::strtol(s, nullptr, 10);
    }
    g_quit = false;
    return true;
}

void shutdown() {
    if (g_texture)  { SDL_DestroyTexture(g_texture);   g_texture  = nullptr; }
    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = nullptr; }
    if (g_window)   { SDL_DestroyWindow(g_window);     g_window   = nullptr; }
    g_pixels.clear();
    g_pixels.shrink_to_fit();
    SDL_Quit();
}

Framebuffer framebuffer() {
    return Framebuffer{ g_pixels.data(), g_fb_w, g_fb_h, g_fb_w };
}

void present() {
    SDL_UpdateTexture(g_texture, nullptr, g_pixels.data(),
                      g_fb_w * static_cast<int>(sizeof(uint32_t)));
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
}

const InputState& input() { return g_input; }

bool init_audio() {
    // M0 seam: no-op. Real device + mixing arrives at M2.
    return true;
}

bool should_quit()  { return g_quit; }
void request_quit() { g_quit = true; }

void run(const std::function<void(double)>& frame) {
    uint64_t     prev = SDL_GetPerformanceCounter();
    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    long         frames = 0;

    while (!g_quit) {
        pump_events();

        const uint64_t now = SDL_GetPerformanceCounter();
        const double   dt  = static_cast<double>(now - prev) / freq;
        prev = now;

        frame(dt);
        present();

        if (g_max_frames >= 0 && ++frames >= g_max_frames) g_quit = true;
    }
}

} // namespace platform
