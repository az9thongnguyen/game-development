# Chapter 02 — The Platform Layer: Window, Framebuffer, Present

> **Goal of this chapter.** Open a real window, allocate the **framebuffer** (the
> array of pixels we own and draw into), and push it to the screen — and set up the
> **platform seam**, the single most important architectural decision in the whole
> project. By the end you'll have a window you filled, pixel by pixel, yourself.

---

## 1. A framebuffer is just a big array of pixels

A screen is a grid of colored dots. To draw anything — a line, a sprite, a 3D
triangle — we keep our *own* grid in memory and write colors into it. That grid is
the **framebuffer**: a flat array of 32-bit integers, one integer per pixel.

Ours is `480 × 270` pixels. It is stored **row-major**: all of row 0 left-to-right,
then all of row 1, and so on. So the pixel at column `x`, row `y` lives at:

```
   index = y * width + x          // width = 480

   (0,0) ── (1,0) ── (2,0) ── …   row 0  →  indices 0,1,2,…,479
   (0,1) ── (1,1) ── …            row 1  →  indices 480,481,…
   (0,2) ── …                     row 2  →  indices 960,…
```

Worked example: the pixel at `(x=10, y=3)` is at index `3*480 + 10 = 1450`. To set
it red: `fb.pixels[1450] = 0xFFFF0000;`.

**Pitch.** We also carry a `pitch` (pixels per row). For us `pitch == width`,
because our rows are tightly packed. But some surfaces pad each row for alignment,
so the *correct* general formula is `index = y * pitch + x`. Writing it with
`pitch` from the start means our renderer code stays correct even if the buffer
layout changes later. (Memory cost, for the curious: `480*270*4 bytes ≈ 506 KB` —
tiny, allocated once.)

### The pixel format: ARGB8888 (and a word on endianness)

Each pixel packs four 8-bit channels into one `uint32_t` — **A**lpha, **R**ed,
**G**reen, **B**lue:

```
  bits:  31..24   23..16   15..8    7..0
         [  A  ]  [  R  ]  [  G  ]  [  B  ]

  0xFF6495ED  →  A=0xFF (opaque)  R=0x64(100)  G=0x95(149)  B=0xED(237) = cornflower
  0xFF000000  →  opaque black     |  0x00FFFFFF → fully TRANSPARENT white
```

Pack and unpack are just shifts and masks:

```cpp
uint32_t argb = (a<<24) | (r<<16) | (g<<8) | b;   // pack
uint8_t  r    = (argb >> 16) & 0xFF;              // unpack one channel
```

> **The classic beginner trap:** forgetting alpha. `0x00FF0000` is "red, fully
> transparent" — it may render as nothing or black depending on the path. When you
> mean "opaque", the top byte must be `0xFF`. Almost every "why is my drawing
> invisible?" bug is a missing `0xFF` alpha.

**Endianness, briefly.** We always *think* of a pixel as the number `0xAARRGGBB`.
In memory on a little-endian Mac the four bytes are actually stored `B, G, R, A`.
We don't have to care, because we hand SDL the token `SDL_PIXELFORMAT_ARGB8888`,
which tells it our *logical* channel order; SDL maps it correctly regardless of
byte order. Rule of thumb: reason in terms of the `0xAARRGGBB` number, never in
terms of raw byte offsets.

---

## 2. The platform seam (the big idea)

The engine needs the OS for four things: a window, a way to show pixels, input, and
time. We isolate **all** of it behind one header, `platform.hpp`, and put the
OS-specific code in a swappable **backend**:

```
        engine + games          ← only ever #include "platform/platform.hpp"
             │  (depends on)
             ▼
        platform.hpp             ← pure interface, ZERO SDL types
             ▲  (implemented by)
   ┌─────────┴──────────┐
 backend_sdl.cpp     backend_web.cpp   (added at M5)
 (desktop, today)    (browser, later)
```

The dependency arrows only ever point **down**: games depend on the engine, the
engine depends on `platform.hpp`, and `platform.hpp` depends on nothing. The
backend depends *up* on the interface to implement it. The strict, enforceable
rule that makes this real:

> **Only a backend file may `#include <SDL.h>`.** No SDL type ever appears in
> `platform.hpp` or anywhere above it.

Why pay for this discipline now? Because at M5 we add a *second* backend for the
browser. If the seam is clean, the browser backend is a new file and **nothing in
the engine or games changes**. "Port to web without a rewrite" is only realistic
if the seam exists from day one — retrofitting it later means touching everything.
You can even check the rule mechanically: `grep -rEn 'SDL_[A-Za-z]' src/engine src/games`
should print nothing. (A plain `"SDL"` search also turns up a few *comments* that mention
SDL — e.g. "loads images WITHOUT SDL_image" — so match real `SDL_` usage, not the word.)

---

## 3. The interface: `platform.hpp`

Read it as a contract:

```cpp
struct Framebuffer { uint32_t* pixels; int width, height, pitch; };
struct Config {
    const char* title;
    int  fb_width, fb_height, scale;
    bool smooth   = false;  // present scaling: false = nearest (retro), true = linear
    bool highdpi  = true;   // render at the display's full pixel density
};

bool init(const Config&);   void shutdown();
Framebuffer framebuffer();  void present();
void run(const std::function<void(double dt)>& frame);
bool should_quit();         void request_quit();
```

Notice what's *absent*: any `SDL_` type. `Framebuffer` is plain pointers and ints;
`run` takes a plain callback. That absence is the seam doing its job. `fb_width`/
`fb_height` are the **logical** render resolution; `scale` is the integer factor
the window is blown up by (480×270 ×2 → a 960×540 window).

The last two fields choose *how the buffer is scaled to the window* — added once the
project grew past the retro M0 demo:

- **`smooth`** — `false` gives crisp, blocky **nearest-neighbor** upscaling (perfect
  for the 480×270 pixel-art demo); `true` gives **linear** filtering, which looks
  right when the framebuffer is already large and we just want it to fill the window
  without hard pixel edges (chess, fps, the 3D core, the iso sim all set `smooth = true`).
- **`highdpi`** — on a Retina display, render into the panel's *real* pixels instead
  of a 1× scaled-up blur, so text and edges stay sharp.

The M0 demo leaves both at the retro defaults; the later large-window scenes opt in
to `smooth = true, highdpi = true`. Same interface, two looks.

---

## 4. The implementation: `backend_sdl.cpp`

This is the *only* file that includes SDL. Its state is three SDL objects plus our
pixel array:

```cpp
SDL_Window*           g_window;    // the OS window
SDL_Renderer*         g_renderer;  // used ONLY to upload + blit our texture
SDL_Texture*          g_texture;   // a GPU-side streaming copy of our framebuffer
std::vector<uint32_t> g_pixels;    // <-- THE framebuffer (ARGB8888), the real thing
```

### How a frame reaches the screen: `present()`

Our pixels live in CPU memory (`g_pixels`). The display is driven by the GPU. So
"show it" means *copy CPU pixels to the GPU and blit them*:

```
  g_pixels (CPU)  ──SDL_UpdateTexture──▶  g_texture (GPU)
                                              │ SDL_RenderCopy (stretch to window)
                                              ▼
                                      back buffer ──SDL_RenderPresent──▶ screen
```

```cpp
SDL_UpdateTexture(g_texture, nullptr, g_pixels.data(), g_fb_w * 4); // CPU → GPU (4 bytes/pixel)
SDL_RenderClear(g_renderer);
SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);            // scale to fill window
SDL_RenderPresent(g_renderer);                                       // flip back buffer to display
```

`SDL_RenderPresent` flips a **back buffer** to the screen — the image you were
building is shown all at once, so you never see a half-drawn frame (tearing). With
vsync enabled, the flip waits for the monitor's refresh, which also paces us to
~60 FPS.

> **"Isn't using `SDL_Renderer` cheating on the no-SDL-drawing rule?"** No — this is
> the one sanctioned use. We are not asking SDL to draw *shapes*; we hand it a
> single finished image (our whole framebuffer) and ask it to copy that one
> rectangle to the window. Every pixel inside was computed by us. On the web this
> exact step becomes "copy the buffer into a `<canvas>`".

### Scaling the framebuffer to the window

The same three SDL settings control upscaling, but their values come from the
`Config` flags so one backend serves both the retro demo and the large, smooth
scenes:

```cpp
SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, cfg.smooth ? "linear" : "nearest");
SDL_RenderSetLogicalSize(g_renderer, g_fb_w, g_fb_h);            // window "is" fb_w×fb_h
SDL_RenderSetIntegerScale(g_renderer, cfg.smooth ? SDL_FALSE : SDL_TRUE);
```

- **`"nearest"` + integer scale** (the demo, `smooth = false`): each source pixel
  becomes an exact N×N block — crisp, no smearing. The right choice for 480×270 pixel
  art shown at 2×/3×.
- **`"linear"`, no integer constraint** (`smooth = true`): neighbours are blended and
  the framebuffer may scale by a fractional factor to fill the window. The right
  choice when the framebuffer is *already* large (chess at 980×720, the 3D core at
  960×600) and we just want it to fit the window cleanly.

HiDPI ties in at window creation: the `SDL_WINDOW_ALLOW_HIGHDPI` flag is set when
`cfg.highdpi` is true, so on a Retina panel SDL gives us the full backing-store
resolution to present into instead of a 1× upscale.

```cpp
const Uint32 flags = SDL_WINDOW_SHOWN | (cfg.highdpi ? SDL_WINDOW_ALLOW_HIGHDPI : 0u);
```

(The buffer upload itself is format-agnostic about all this — `SDL_UpdateTexture`
takes the row pitch in *bytes*, `g_fb_w * sizeof(uint32_t)`; the renderer does the
scaling on the way to the window.)

### Keeping `main()` ours: `SDL_MAIN_HANDLED`

On some platforms SDL `#define`s `main` to its own `SDL_main` and expects you to
link `SDL2main`, so it can run setup before your code. We don't want that magic:

```cpp
#define SDL_MAIN_HANDLED   // tell SDL: "I provide a normal int main()"
#include <SDL.h>
...
SDL_SetMainReady();        // (in init) "I've done any pre-init myself"
```

This keeps `main.cpp` a plain `int main()` that includes only `platform.hpp`, and
avoids an extra link dependency. It's a small thing that keeps the seam clean.

### Ownership & lifetime (no leaks)

We allocate the framebuffer **once** in `init` and reuse it every frame:

```cpp
g_pixels.assign(size_t(g_fb_w) * g_fb_h, 0xFF000000u);  // once, opaque black
```

Allocating per frame would create garbage, fragment memory, and cause stutter — and
the requirements forbid per-frame allocation in the render path. `shutdown()`
tears everything down in reverse order (texture → renderer → window → `SDL_Quit`)
and frees the buffer, so the program exits with no leaks. (At M0 acceptance we'll
*prove* that with a leak checker.)

### A testing aid: `HAND_ENGINE_FRAMES`

`run()` reads the environment variable `HAND_ENGINE_FRAMES`. If set to a number,
the loop quits after that many frames. That lets us run head-less — no human
closing the window — which is exactly what automated checks and the M0 leak test
need:

```sh
HAND_ENGINE_FRAMES=60 ./build/demo   # runs ~60 frames, exits 0
```

(The frame loop itself — and *why* it lives here in the platform layer — is the
subject of Chapter 03. For now: `run()` calls your `frame(dt)` once per frame and
presents.)

---

## 5. Run & observe

```sh
cmake --build build
./build/demo
```

A **960×540 window** titled *"hand-engine — M0"* opens, filled solid **cornflower
blue**. Close it with the red button. Those pixels came from this loop in
`main.cpp`:

```cpp
platform::Framebuffer fb = platform::framebuffer();
for (int i = 0; i < fb.width * fb.height; ++i)
    fb.pixels[i] = 0xFF6495ED;   // every single pixel, set by us
```

You are now in complete control of every dot on that surface.

---

## 6. Common pitfalls

- **Missing alpha** → invisible/black drawing. Opaque means top byte `0xFF`.
- **Row math with `width` when you mean `pitch`** → a slanted/garbled image if the
  buffer is ever padded. Always `y * pitch + x`.
- **Writing outside the buffer** (`x ≥ width`, `y ≥ height`, or a negative) →
  memory corruption. Clipping (Chapter 05) and ASan (Chapter 01) are your safety
  nets.
- **Allocating in the frame path** → stutter. Allocate once in `init`.
- **Reaching for `SDL_RenderDrawLine` etc.** → that's the rule we don't break; we
  draw lines ourselves in Chapter 05.

---

## 7. Glossary

- **Framebuffer** — the CPU array of pixels we draw into.
- **Pixel format (ARGB8888)** — 4 channels × 8 bits packed in a `uint32_t`.
- **Pitch / stride** — bytes (or pixels) per row; ≥ width.
- **Texture (streaming)** — a GPU image we update every frame from the framebuffer.
- **Back buffer / present / vsync** — draw off-screen, flip to display, synced to
  refresh.
- **Backend** — a concrete implementation of `platform.hpp` (SDL now, web later).

---

## 8. Exercises

1. **Recolor.** Set every pixel to `0xFFFF0000` (red). Then derive the hex for pure
   green and pure blue. *(Hint: which byte is which channel?)*
2. **Hand-draw a gradient.** Replace the solid fill with nested loops over `y` then
   `x`, setting `r = x * 255 / fb.width` and `g = y * 255 / fb.height`. You'll see a
   smooth ramp — proof you own every pixel. *(Hint: index is `y*fb.pitch + x`; keep
   alpha `0xFF`.)*
3. **Plot one dot.** Set just the pixel at `(x=240, y=135)` to white on a black
   background. Compute its index by hand first, then check your loop hits it.
4. **Resize without touching draw code.** Change `Config` to `320×180`, `scale 3`.
   The window size changes; your drawing code doesn't. Why is that a good sign for
   the architecture?

---

## 9. What's next

We have a window and full control of its pixels — but the screen is static and the
program structure is ad-hoc. **Chapter 03** turns `run()` into a proper **game
loop** with a **fixed-timestep** update, introduces the `Scene`/`App` structure
every game will build on, and explains why the loop lives down here in the platform
layer (it's the key to the web port).
