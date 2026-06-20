// =============================================================================
//  platform/backend_sdl.cpp  —  the SDL2 implementation of platform.hpp
// =============================================================================
//  This is the ONLY file in the project allowed to #include <SDL.h>. It uses SDL
//  for exactly the "thin shim" responsibilities: create a window, hand us a pixel
//  buffer + push it to the screen, run the loop, and report time. We deliberately
//  do NOT use any SDL drawing primitives — the only SDL_Renderer calls here exist
//  to upload our own framebuffer as a texture and blit that single texture to the
//  window. Every actual pixel is written by our software renderer.
// =============================================================================
#define SDL_MAIN_HANDLED          // we provide a normal int main(); see note below
#include <SDL.h>

#include "platform/platform.hpp"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace platform {
namespace {

// All backend state is file-local. The engine never sees these; it only sees the
// functions declared in platform.hpp.
SDL_Window*           g_window   = nullptr;
SDL_Renderer*         g_renderer = nullptr;  // used ONLY to present the texture
SDL_Texture*          g_texture  = nullptr;  // streaming texture we upload into
std::vector<uint32_t> g_pixels;              // <-- THE framebuffer (ARGB8888)
int                   g_fb_w = 0;
int                   g_fb_h = 0;
bool                  g_quit  = false;

// Testing aid: if HAND_ENGINE_FRAMES=N is set in the environment, the loop quits
// after N frames. Lets us run head-less (CI, leak checks) without a human closing
// the window. -1 means "run until quit".
long g_max_frames = -1;

void pump_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            g_quit = true;   // window close button / Cmd-Q
        } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
            g_quit = true;   // convenience kill-switch; full input arrives in Step 6
        }
        // Other keyboard/mouse handling arrives in the input chapter (Step 6).
    }
}

} // anonymous namespace

bool init(const Config& cfg) {
    // Because we compiled with SDL_MAIN_HANDLED, SDL doesn't hijack main(); we
    // must tell it we're ready before init. This keeps main() a plain int main()
    // and avoids linking SDL2main.
    SDL_SetMainReady();

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "platform: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    g_fb_w = cfg.fb_width;
    g_fb_h = cfg.fb_height;
    const int win_w = cfg.fb_width  * cfg.scale;
    const int win_h = cfg.fb_height * cfg.scale;

    g_window = SDL_CreateWindow(cfg.title,
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                win_w, win_h, SDL_WINDOW_SHOWN);
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

    // Nearest-neighbour scaling keeps our low-res pixels crisp when the small
    // framebuffer is stretched to fill the larger window. Logical size + integer
    // scale make the upscale an exact whole-number multiple (no blurry edges).
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(g_renderer, g_fb_w, g_fb_h);
    SDL_RenderSetIntegerScale(g_renderer, SDL_TRUE);

    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, g_fb_w, g_fb_h);
    if (!g_texture) {
        std::fprintf(stderr, "platform: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    // Allocate the framebuffer once (opaque black). We never reallocate per frame.
    g_pixels.assign(static_cast<size_t>(g_fb_w) * static_cast<size_t>(g_fb_h),
                    0xFF000000u);

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
    // Upload our CPU pixels into the GPU texture, then blit that one texture to
    // fill the window. This is the only sanctioned use of SDL_Renderer.
    SDL_UpdateTexture(g_texture, nullptr, g_pixels.data(),
                      g_fb_w * static_cast<int>(sizeof(uint32_t)));
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
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

        frame(dt);     // engine does update + render into the framebuffer
        present();     // we push it to the screen

        if (g_max_frames >= 0 && ++frames >= g_max_frames) g_quit = true;
    }
}

} // namespace platform
