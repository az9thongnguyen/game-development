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
#include <string>
#include <vector>

#ifdef __EMSCRIPTEN__
// On the web there is no OS-owned thread we may block: the browser owns the event
// loop, so we hand it a per-frame callback via emscripten_set_main_loop instead of
// spinning a while(true). This is the ONE platform difference the web port needs —
// proof that the tick(dt) design (ch03) was the right call. SDL2 itself is provided
// by Emscripten's port (-sUSE_SDL=2), so the rest of this file is unchanged.
#include <emscripten.h>
#endif

namespace platform {
namespace {

SDL_Window*           g_window   = nullptr;
SDL_Renderer*         g_renderer = nullptr;  // used ONLY to present the texture
SDL_Texture*          g_texture  = nullptr;  // streaming texture we upload into
std::vector<uint32_t> g_pixels;              // <-- THE framebuffer (ARGB8888)
int                   g_fb_w = 0;    // PHYSICAL framebuffer size = logical * supersample
int                   g_fb_h = 0;
int                   g_log_w = 0;   // LOGICAL size the game/mouse reason in
int                   g_log_h = 0;
int                   g_ss    = 1;   // supersample factor
bool                  g_quit  = false;
bool                  g_quit_on_escape = true;
bool                  g_resizable      = false;
SDL_Cursor*           g_cursors[7]     = {};   // one per platform::Cursor, created lazily
Cursor                g_cursor_now     = Cursor::Arrow;
InputState            g_input;

#ifndef __EMSCRIPTEN__
long g_max_frames = -1;  // HAND_ENGINE_FRAMES test hook (-1 = run until quit);
                         // only the desktop loop honors it, so it's desktop-only.
#endif

SDL_AudioDeviceID g_audio_dev  = 0;
int               g_audio_rate = 44100;
bool              g_audio_ok   = false;

// The scancode table. Every Key maps to exactly one scancode, and the pump loops
// over this — so adding a key is one row here plus one enumerator, nothing else.
struct KeyMap { Key k; SDL_Scancode sc; };

const std::vector<KeyMap>& kmap_repeat() {
    static const std::vector<KeyMap> t = {
        {Key::Up, SDL_SCANCODE_UP}, {Key::Down, SDL_SCANCODE_DOWN},
        {Key::Left, SDL_SCANCODE_LEFT}, {Key::Right, SDL_SCANCODE_RIGHT},
        {Key::Space, SDL_SCANCODE_SPACE}, {Key::Enter, SDL_SCANCODE_RETURN},
        {Key::Escape, SDL_SCANCODE_ESCAPE},
        {Key::A, SDL_SCANCODE_A}, {Key::B, SDL_SCANCODE_B}, {Key::C, SDL_SCANCODE_C},
        {Key::D, SDL_SCANCODE_D}, {Key::E, SDL_SCANCODE_E}, {Key::F, SDL_SCANCODE_F},
        {Key::G, SDL_SCANCODE_G}, {Key::H, SDL_SCANCODE_H}, {Key::I, SDL_SCANCODE_I},
        {Key::J, SDL_SCANCODE_J}, {Key::K, SDL_SCANCODE_K}, {Key::L, SDL_SCANCODE_L},
        {Key::M, SDL_SCANCODE_M}, {Key::N, SDL_SCANCODE_N}, {Key::O, SDL_SCANCODE_O},
        {Key::P, SDL_SCANCODE_P}, {Key::Q, SDL_SCANCODE_Q}, {Key::R, SDL_SCANCODE_R},
        {Key::S, SDL_SCANCODE_S}, {Key::T, SDL_SCANCODE_T}, {Key::U, SDL_SCANCODE_U},
        {Key::V, SDL_SCANCODE_V}, {Key::W, SDL_SCANCODE_W}, {Key::X, SDL_SCANCODE_X},
        {Key::Y, SDL_SCANCODE_Y}, {Key::Z, SDL_SCANCODE_Z},
        {Key::Num0, SDL_SCANCODE_0}, {Key::Num1, SDL_SCANCODE_1}, {Key::Num2, SDL_SCANCODE_2},
        {Key::Num3, SDL_SCANCODE_3}, {Key::Num4, SDL_SCANCODE_4}, {Key::Num5, SDL_SCANCODE_5},
        {Key::Num6, SDL_SCANCODE_6}, {Key::Num7, SDL_SCANCODE_7}, {Key::Num8, SDL_SCANCODE_8},
        {Key::Num9, SDL_SCANCODE_9},
        {Key::Minus, SDL_SCANCODE_MINUS}, {Key::Equals, SDL_SCANCODE_EQUALS},
        {Key::Tab, SDL_SCANCODE_TAB}, {Key::Delete, SDL_SCANCODE_DELETE},
        {Key::Backspace, SDL_SCANCODE_BACKSPACE},
        {Key::Home, SDL_SCANCODE_HOME}, {Key::End, SDL_SCANCODE_END},
        {Key::PageUp, SDL_SCANCODE_PAGEUP}, {Key::PageDown, SDL_SCANCODE_PAGEDOWN},
        {Key::F1, SDL_SCANCODE_F1}, {Key::F2, SDL_SCANCODE_F2}, {Key::F3, SDL_SCANCODE_F3},
        {Key::F4, SDL_SCANCODE_F4}, {Key::F5, SDL_SCANCODE_F5}, {Key::F6, SDL_SCANCODE_F6},
        {Key::F7, SDL_SCANCODE_F7}, {Key::F8, SDL_SCANCODE_F8}, {Key::F9, SDL_SCANCODE_F9},
        {Key::F10, SDL_SCANCODE_F10}, {Key::F11, SDL_SCANCODE_F11}, {Key::F12, SDL_SCANCODE_F12},
    };
    return t;
}

// A resizable window changes the LOGICAL size, so the CPU framebuffer and the
// streaming texture both have to be rebuilt. Nothing above the platform layer sees
// this beyond InputState::window_resized — a scene lays itself out from
// Renderer2D::width()/height() every frame anyway.
void on_window_resized() {
    if (!g_resizable || !g_window) return;
    int ww = 0, wh = 0;
    SDL_GetWindowSize(g_window, &ww, &wh);
    if (ww <= 0 || wh <= 0) return;
    if (ww == g_log_w && wh == g_log_h) return;

    g_log_w = ww;
    g_log_h = wh;
    g_fb_w  = g_log_w * g_ss;
    g_fb_h  = g_log_h * g_ss;
    g_pixels.assign(static_cast<size_t>(g_fb_w) * static_cast<size_t>(g_fb_h), 0xFF000000u);

    SDL_RenderSetLogicalSize(g_renderer, g_log_w, g_log_h);
    if (g_texture) SDL_DestroyTexture(g_texture);
    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, g_fb_w, g_fb_h);

    g_input.window_resized = true;
    g_input.win_w = g_log_w;
    g_input.win_h = g_log_h;
}

void pump_events() {
    // Per-frame transients: anything that describes "what happened this frame" must
    // be cleared BEFORE the queue is drained, or it accumulates forever.
    g_input.text_len       = 0;
    g_input.wheel_x        = 0;
    g_input.wheel_y        = 0;
    g_input.window_resized = false;
    for (bool& r : g_input.key_repeat) r = false;

    // Pump the OS queue (this also updates SDL's internal keyboard/mouse state, which
    // the polling below reads) and pick out the things polling cannot tell us:
    // committed text, wheel ticks, auto-repeat, and resizes.
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                g_quit = true;
                break;

            case SDL_TEXTINPUT: {
                // What the OS decided the keystrokes MEAN — after dead keys, compose
                // sequences, IME candidates and whatever layout the user actually has.
                // A text field must read this rather than reconstruct characters from
                // key edges, which only ever works for US-ASCII typists.
                const char* t = e.text.text;
                for (std::size_t i = 0; t[i] && g_input.text_len < InputState::kTextMax; ++i)
                    g_input.text[g_input.text_len++] = t[i];
                break;
            }

            case SDL_MOUSEWHEEL: {
                const int dir = (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) ? -1 : 1;
                g_input.wheel_x += e.wheel.x * dir;
                g_input.wheel_y += e.wheel.y * dir;
                break;
            }

            case SDL_KEYDOWN:
                // Only the repeat flag comes from the event; the down/pressed/released
                // edges below stay poll-derived so held keys behave identically to
                // before this change.
                if (e.key.repeat) {
                    for (const auto& m : kmap_repeat())
                        if (m.sc == e.key.keysym.scancode) { g_input.key_repeat[int(m.k)] = true; break; }
                }
                break;

            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) on_window_resized();
                break;

            default:
                break;
        }
    }

    // ---- Keyboard: poll state, edge-detect against last frame ----
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    for (const KeyMap& m : kmap_repeat()) {
        const bool now = ks[m.sc] != 0;
        const bool was = g_input.key_down[int(m.k)];
        g_input.key_pressed[int(m.k)]  = now && !was;
        g_input.key_released[int(m.k)] = !now && was;
        g_input.key_down[int(m.k)]     = now;
    }
    if (g_quit_on_escape && g_input.key_down[int(Key::Escape)]) g_quit = true;

    const SDL_Keymod km = SDL_GetModState();
    g_input.mods.shift = (km & KMOD_SHIFT) != 0;
    g_input.mods.ctrl  = (km & KMOD_CTRL)  != 0;
    g_input.mods.alt   = (km & KMOD_ALT)   != 0;
    g_input.mods.super = (km & KMOD_GUI)   != 0;

    // ---- Mouse: poll position + buttons; map window points -> framebuffer ----
    int mx = 0, my = 0;
    const Uint32 mask = SDL_GetMouseState(&mx, &my);
    int ww = g_fb_w, wh = g_fb_h;
    SDL_GetWindowSize(g_window, &ww, &wh);
    g_input.mouse_x = (ww > 0) ? mx * g_log_w / ww : mx;   // map to LOGICAL space (not physical)
    g_input.mouse_y = (wh > 0) ? my * g_log_h / wh : my;
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

    g_ss    = cfg.supersample < 1 ? 1 : (cfg.supersample > 4 ? 4 : cfg.supersample);
    g_log_w = cfg.fb_width;
    g_log_h = cfg.fb_height;
    g_fb_w  = g_log_w * g_ss;    // physical framebuffer we actually rasterize into
    g_fb_h  = g_log_h * g_ss;
    const int win_w = cfg.fb_width  * cfg.scale;   // the window stays at logical*scale
    const int win_h = cfg.fb_height * cfg.scale;

    g_quit_on_escape = cfg.quit_on_escape;
    // A resizable window is a DESKTOP idea. On the web the canvas is an element the
    // page sizes, and asking SDL for SDL_WINDOW_RESIZABLE makes it adopt that element's
    // current CSS size instead of the one requested — which is how the Studio came up
    // as a 3x3 canvas in a browser, i.e. invisible. The page scales the canvas to fit
    // already, so the honest translation of "resizable" here is "let the page do it".
    //
    // The decision lives at the platform seam, not in the game: --shell asks for a
    // workspace that fits the screen it is on, and what that means per platform is
    // exactly what this layer is for.
#ifdef __EMSCRIPTEN__
    const bool resizable = false;
#else
    const bool resizable = cfg.resizable;
#endif
    g_resizable      = resizable;
    const Uint32 win_flags = SDL_WINDOW_SHOWN |
                             (cfg.highdpi ? SDL_WINDOW_ALLOW_HIGHDPI : 0u) |
                             (resizable ? SDL_WINDOW_RESIZABLE : 0u);
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
        // No accelerated driver. On the web that means WebGL is unavailable — a browser
        // with it disabled, a machine with no GPU, a headless run — and the whole app
        // used to die there with one line on a console nobody reads. Everything above
        // this layer draws into a CPU framebuffer anyway; the renderer's entire job is
        // to put ONE textured quad on the screen, which software does perfectly well.
        std::fprintf(stderr, "platform: no accelerated renderer (%s); using software\n",
                     SDL_GetError());
        g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!g_renderer) {
        std::fprintf(stderr, "platform: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return false;
    }
    // Say which one we got. A screenshot that came out aliased and a screenshot that
    // came out smooth are different evidence, and the difference is this line.
    if (SDL_RendererInfo info; SDL_GetRendererInfo(g_renderer, &info) == 0)
        std::fprintf(stderr, "platform: renderer '%s'%s\n", info.name,
                     (info.flags & SDL_RENDERER_ACCELERATED) ? " (accelerated)" : " (software)");

    // Smooth (linear) present for real artwork; nearest for crisp retro pixels.
    // SSAA (g_ss>1) REQUIRES linear + non-integer so the physical texture is
    // box-ish downsampled to the logical rect (that downsample IS the anti-alias).
    const bool linear = cfg.smooth || g_ss > 1;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear ? "linear" : "nearest");
    SDL_RenderSetLogicalSize(g_renderer, g_log_w, g_log_h);   // logical → RenderCopy downsamples to it
    SDL_RenderSetIntegerScale(g_renderer, linear ? SDL_FALSE : SDL_TRUE);

    g_texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING, g_fb_w, g_fb_h);
    if (!g_texture) {
        std::fprintf(stderr, "platform: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return false;
    }

    g_pixels.assign(static_cast<size_t>(g_fb_w) * static_cast<size_t>(g_fb_h), 0xFF000000u);

    // Ask the OS to deliver composed text. Without this there are no SDL_TEXTINPUT
    // events at all, and a text field can only ever see raw key edges.
    SDL_StartTextInput();
    g_input.win_w = g_log_w;
    g_input.win_h = g_log_h;

#ifndef __EMSCRIPTEN__
    if (const char* s = std::getenv("HAND_ENGINE_FRAMES")) {
        g_max_frames = std::strtol(s, nullptr, 10);
    }
#endif
    g_quit = false;
    return true;
}

void set_cursor(Cursor c) {
    // Setting the same shape every frame is the natural way for an immediate-mode UI
    // to drive this, so make the repeat case free.
    if (c == g_cursor_now && g_cursors[static_cast<int>(c)]) return;

    static const SDL_SystemCursor kSys[] = {
        SDL_SYSTEM_CURSOR_ARROW, SDL_SYSTEM_CURSOR_HAND,   SDL_SYSTEM_CURSOR_IBEAM,
        SDL_SYSTEM_CURSOR_SIZEWE, SDL_SYSTEM_CURSOR_SIZENS, SDL_SYSTEM_CURSOR_SIZEALL,
        SDL_SYSTEM_CURSOR_CROSSHAIR,
    };
    const int i = static_cast<int>(c);
    if (i < 0 || i >= 7) return;
    if (!g_cursors[i]) g_cursors[i] = SDL_CreateSystemCursor(kSys[i]);
    if (!g_cursors[i]) return;                 // creation failed: keep whatever we had
    SDL_SetCursor(g_cursors[i]);
    g_cursor_now = c;
}

std::string clipboard_get() {
    if (SDL_HasClipboardText() == SDL_FALSE) return {};
    char* t = SDL_GetClipboardText();          // SDL allocates; we own it
    if (!t) return {};
    std::string out(t);
    SDL_free(t);
    return out;
}

void clipboard_set(const std::string& text) { SDL_SetClipboardText(text.c_str()); }

void shutdown() {
    for (SDL_Cursor*& c : g_cursors) { if (c) { SDL_FreeCursor(c); c = nullptr; } }
    g_cursor_now = Cursor::Arrow;
    if (g_texture)  { SDL_DestroyTexture(g_texture);   g_texture  = nullptr; }
    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = nullptr; }
    if (g_window)   { SDL_DestroyWindow(g_window);     g_window   = nullptr; }
    g_pixels.clear();
    g_pixels.shrink_to_fit();
    if (g_audio_dev) { SDL_CloseAudioDevice(g_audio_dev); g_audio_dev = 0; g_audio_ok = false; }
    SDL_Quit();
}

Framebuffer framebuffer() {
    return Framebuffer{ g_pixels.data(), g_fb_w, g_fb_h, g_fb_w };
}

int supersample() { return g_ss; }

void present() {
    // A resize recreates the texture; if that ever fails there is nothing to present
    // and SDL_UpdateTexture(nullptr, ...) would take the process down with it.
    if (!g_texture) return;
    SDL_UpdateTexture(g_texture, nullptr, g_pixels.data(),
                      g_fb_w * static_cast<int>(sizeof(uint32_t)));
    SDL_RenderClear(g_renderer);
    SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);
    SDL_RenderPresent(g_renderer);
}

const InputState& input() { return g_input; }

bool init_audio() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "platform: audio init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = 44100;
    want.format   = AUDIO_S16SYS;  // signed 16-bit, native byte order
    want.channels = 1;             // mono
    want.samples  = 1024;
    want.callback = nullptr;       // we push samples with SDL_QueueAudio
    g_audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audio_dev == 0) {
        std::fprintf(stderr, "platform: open audio failed: %s\n", SDL_GetError());
        return false;
    }
    g_audio_rate = have.freq;
    SDL_PauseAudioDevice(g_audio_dev, 0);  // start playing
    g_audio_ok = true;
    return true;
}

int audio_rate() { return g_audio_rate; }

void play_sound(const int16_t* samples, int count) {
    if (!g_audio_ok || samples == nullptr || count <= 0) return;
    // Drop new clips if a lot is already queued (no callback mixer in M2). Guard a
    // bad/zero reported rate so the cast can't wrap to a huge limit.
    const Uint32 queue_cap = (g_audio_rate > 0) ? static_cast<Uint32>(g_audio_rate) : 44100u;
    if (SDL_GetQueuedAudioSize(g_audio_dev) > queue_cap) return;
    SDL_QueueAudio(g_audio_dev, samples, static_cast<Uint32>(count) * sizeof(int16_t));
}

bool should_quit()  { return g_quit; }
void request_quit() { g_quit = true; }

#ifdef __EMSCRIPTEN__
namespace {
std::function<void(double)> g_frame;     // the App::frame callback, held across ticks
uint64_t                    g_prev = 0;
double                      g_freq = 1.0;

// The browser calls this once per animation frame (via requestAnimationFrame).
void emscripten_tick() {
    pump_events();
    const uint64_t now = SDL_GetPerformanceCounter();
    double         dt  = static_cast<double>(now - g_prev) / g_freq;
    g_prev = now;
    if (dt > 0.25) dt = 0.25;            // same spiral-of-death clamp as App::frame
    g_frame(dt);
    present();
    if (g_quit) emscripten_cancel_main_loop();
}
} // namespace
#endif

void run(const std::function<void(double)>& frame) {
#ifdef __EMSCRIPTEN__
    g_frame = frame;
    g_prev  = SDL_GetPerformanceCounter();
    g_freq  = static_cast<double>(SDL_GetPerformanceFrequency());
    // fps=0 → drive from requestAnimationFrame (vsync); simulate_infinite_loop=1 →
    // this call does not return (the browser keeps calling emscripten_tick), so the
    // engine/game code above is byte-for-byte the same as on desktop.
    emscripten_set_main_loop(emscripten_tick, 0, 1);
#else
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
#endif
}

} // namespace platform
