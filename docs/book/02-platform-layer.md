# Chapter 02 — The Platform Layer: Window, Framebuffer, Present

> **What you'll learn:** what a *framebuffer* is and why we own it; how a window
> appears and how we push our pixels onto it; and the single most important
> architectural idea in this whole project — the **platform seam** that will let
> us port to the web later without rewriting anything.

---

## 1. The idea

### A framebuffer is just a big array of pixels

A screen is a grid of pixels. To draw, we keep our own grid in memory — a plain
array of 32-bit integers, one per pixel — and we write colors into it. That array
is the **framebuffer**. Drawing a line, a sprite, or a 3D triangle all come down
to the same thing: *computing which array elements to set, and to what color.*

We use the format **ARGB8888**: each pixel is one `uint32_t` holding four 8-bit
channels — Alpha, Red, Green, Blue:

```
  bits:  31..24   23..16   15..8    7..0
         [  A  ]  [  R  ]  [  G  ]  [  B  ]
  e.g.   0xFF      0x64     0x95     0xED    ->  opaque cornflower blue
```

Our framebuffer is `480 × 270 = 129,600` pixels, stored row-major (row 0 left to
right, then row 1, …). The pixel at `(x, y)` lives at index `y * width + x`. We
expose this as a `Framebuffer` struct: a pointer plus width/height/pitch.

> **Why a small resolution (480×270) scaled up?** Software rendering means the CPU
> touches every pixel every frame. Fewer pixels = more headroom to do interesting
> work by hand. We then upscale by an integer factor (×2 → 960×540) so it fills a
> reasonable window while staying pixel-crisp. This is the classic retro/pixel-art
> approach, and it keeps M0–M2 comfortably at 60 FPS.

### The platform seam (the big idea)

Our engine needs the OS for four things: a window, a way to show pixels, input,
and time. We isolate **all** of that behind one header, `platform.hpp`, and put
the actual OS-specific code in a separate "backend" file:

```
   engine + games   →   platform.hpp     ← they only ever see this
                            ↑
              ┌─────────────┴─────────────┐
        backend_sdl.cpp            backend_web.cpp   (added at M5)
       (desktop, today)            (browser, later)
```

The strict rule: **only the backend file may `#include <SDL.h>`.** Engine and game
code never see an SDL type. Why bother? Because at M5 we add a *second* backend for
the browser, and if the seam is clean, **none of the engine or game code changes**.
This single discipline is what makes "port to web without a rewrite" realistic
rather than wishful.

---

## 2. The code

### `platform.hpp` — the interface

Read it as a contract. The important pieces:

- `struct Framebuffer { uint32_t* pixels; int width, height, pitch; };` — the
  pixel grid, handed to whoever is drawing.
- `struct Config` — title + logical resolution + integer scale.
- `init` / `shutdown` — bring the window up and tear it down.
- `framebuffer()` — get the buffer to draw into this frame.
- `present()` — push the buffer to the screen.
- `run(frame)` — the main loop (covered in Chapter 03).

Notice there is **no SDL type anywhere** in this header. That's the seam working.

### `backend_sdl.cpp` — the SDL implementation

This is the only file that includes SDL. Three SDL objects do the work:

- an `SDL_Window` — the actual OS window,
- an `SDL_Texture` (streaming, ARGB8888, 480×270) — a GPU-side copy of our buffer,
- an `SDL_Renderer` — used **only** to upload the texture and blit it.

`present()` is the whole "show it on screen" story:

```cpp
SDL_UpdateTexture(g_texture, nullptr, g_pixels.data(), g_fb_w * 4); // CPU → GPU
SDL_RenderClear(g_renderer);
SDL_RenderCopy(g_renderer, g_texture, nullptr, nullptr);            // stretch to fill
SDL_RenderPresent(g_renderer);                                       // flip to display
```

> **Isn't using `SDL_Renderer` cheating?** This is the one sanctioned use. We are
> not asking SDL to draw shapes — we hand it a single finished image (our
> framebuffer) and ask it to blit that one rectangle. Every pixel inside it was
> computed by us. On the web, this exact step becomes "copy the buffer to a
> `<canvas>`".

A few setup details worth understanding:

- `SDL_RenderSetLogicalSize(r, 480, 270)` + `SDL_RenderSetIntegerScale(r, true)` +
  `SDL_HINT_RENDER_SCALE_QUALITY = "nearest"` together give a crisp, exact 2×
  upscale with no blur.
- `#define SDL_MAIN_HANDLED` + `SDL_SetMainReady()` let us keep a normal
  `int main()` instead of letting SDL rewrite our entry point. (SDL sometimes
  `#define`s `main` to its own; we opt out so `main.cpp` stays plain and doesn't
  need to link `SDL2main`.)
- The framebuffer is allocated **once** in `init()` (`g_pixels.assign(...)`) and
  reused every frame. Allocating per frame would create garbage and stutter.

### A testing aid: `HAND_ENGINE_FRAMES`

The loop checks an environment variable `HAND_ENGINE_FRAMES`. If set to a number,
the program runs that many frames and exits on its own. That lets us run it
without a human closing the window — useful for automated checks and, later, for
running memory-leak tools at M0 acceptance.

---

## 3. Run & observe

```sh
cmake --build build
./build/demo
```

A **960×540 window** titled *"hand-engine — M0"* opens, filled solid
**cornflower blue**. Close it with the window's red button (ESC works after
Chapter 03's edit). Every one of those pixels was written by this loop in
`main.cpp`:

```cpp
for (int i = 0; i < count; ++i) fb.pixels[i] = 0xFF6495ED; // we set each pixel
```

To run it head-less (no manual close):

```sh
HAND_ENGINE_FRAMES=60 ./build/demo   # runs ~60 frames, exits 0
```

---

## 4. Exercises

1. **Change the color.** Edit the constant in `main.cpp` to `0xFFFF0000` (opaque
   red) and rebuild. Then work out the hex for pure green and pure blue.
2. **Draw a gradient by hand.** Replace the solid fill with a loop over `x` and
   `y` (`for y … for x …`) that sets the red channel from `x` and the green
   channel from `y`. You'll see a smooth color ramp — proof you control every
   pixel. (Index is `y * fb.width + x`.)
3. **Resize.** Change `fb_width/fb_height/scale` in the `Config`. Try `320×180`
   at scale `3`. Notice the window size changes but the drawing code doesn't.

---

## 5. What's next

Right now the screen is static. In **Chapter 03** we turn `run()` into a proper
**game loop** with a **fixed-timestep** update, and introduce the `Scene`/`App`
structure that every game in this project will build on.
