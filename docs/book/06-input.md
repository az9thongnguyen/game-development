# Chapter 06 — Normalized Input

> **Goal of this chapter.** Let the player drive the game. We read keyboard and
> mouse through a **normalized input snapshot** produced by the platform — never
> SDL directly above the seam — and learn the crucial distinction between a key
> being *held*, being *pressed this frame*, and being *released this frame*.

---

## 1. The idea

Raw input from the OS arrives as a stream of **events**: "W went down", "mouse
moved to (x,y)", "left button up". Games rarely want the raw stream; they want to
ask simple questions each frame:

- *Is W held right now?* (for continuous movement)
- *Was Space pressed this exact frame?* (to jump/fire once, not repeatedly)
- *Where is the mouse?* (to aim, or pick a chess square)

So the platform collects the event stream into a tidy **snapshot** — an
`InputState` — once per frame, and the game just reads it. This also fits the
seam: input is "normalized hardware state", produced by the platform, consumed by
everyone above it. No `SDL_` symbol appears above `backend_sdl.cpp`.

### Three states, not one

For every key/button we track three booleans:

| State | Meaning | Use it for |
|-------|---------|-----------|
| `down`     | held *this frame* (a **level**) | continuous actions: walking, holding aim |
| `pressed`  | went up→down *this frame* (an **edge**) | one-shot actions: jump, fire, select |
| `released` | went down→up *this frame* (an **edge**) | "on let go": charge-and-release |

Mixing these up is a classic bug: using `down` for "fire" makes you fire every
frame the key is held (~60×/second); using `pressed` for "walk" makes you step a
single pixel per tap. Pick the right one for the job.

---

## 2. Producing the snapshot (backend)

The snapshot lives in `platform/input.hpp` (`Key`, `MouseButton`, `InputState`).
The SDL backend fills it in `pump_events()`, which `run()` calls once per frame
*before* the frame callback.

### We POLL state, not drain events

You might expect the backend to read the SDL *event queue* (KEYDOWN/KEYUP) and
build the snapshot from it. It doesn't. We instead **poll the current state** each
frame and derive the edges ourselves by comparing against last frame. The event
queue is still pumped — but only to keep the OS happy and to catch the window-close
(`SDL_QUIT`) event:

```cpp
SDL_Event e;
while (SDL_PollEvent(&e)) { if (e.type == SDL_QUIT) g_quit = true; }
```

Why poll instead of consuming key events? Under HiDPI and the `sdl2-compat` shim
(SDL2 API on top of SDL3), key *event* delivery can be flaky, but the *state* query
`SDL_GetKeyboardState` is rock-solid. Polling sidesteps a whole class of
"sometimes a keypress is missed" bugs, and the edge logic is just as simple.

### Deriving edges by comparing to last frame

For the keyboard we loop over a small table mapping our `Key` enum to SDL
scancodes, read each key's *level* now, and compute the edges against the value we
stored last frame:

```cpp
const Uint8* ks = SDL_GetKeyboardState(nullptr);
for (const KeyMap& m : kmap) {
    const bool now = ks[m.sc] != 0;
    const bool was = g_input.key_down[int(m.k)];     // last frame's level
    g_input.key_pressed[int(m.k)]  = now && !was;    // up→down edge
    g_input.key_released[int(m.k)] = !now && was;    // down→up edge
    g_input.key_down[int(m.k)]     = now;            // store for next frame
}
```

The level `down` *is* the freshly polled value; `pressed`/`released` are pure
functions of (now, was). Because `was` is "what `down` held a frame ago", a held
key reports `down == true` every frame but `pressed == true` only on the frame it
first went down — no auto-repeat handling needed (there are no key *events* to
repeat). Adding a new key is one row in the table; the loop handles the rest.

### Mouse: poll position, map window → framebuffer

SDL reports the mouse in *window* pixels, but we draw in the (e.g.) 480×270
*framebuffer*. We convert with a simple ratio against the current window size:

```cpp
int mx, my;
const Uint32 mask = SDL_GetMouseState(&mx, &my);
int ww, wh; SDL_GetWindowSize(g_window, &ww, &wh);
g_input.mouse_x = (ww > 0) ? mx * g_fb_w / ww : mx;     // window px → framebuffer px
g_input.mouse_y = (wh > 0) ? my * g_fb_h / wh : my;
```

Buttons use the same level-vs-last-frame edge logic as keys, reading the bitmask
SDL returns (`mask & SDL_BUTTON_LMASK`, …). Mapping to framebuffer coordinates is
what makes a click line up with the chess square you drew, at any window scale.

The game reaches all of it through `platform::input()` (and, more conveniently,
through the `Context`). `Key`/`InputState` deliberately live in the platform layer
so this respects the one-way dependency rule — no `SDL_` symbol leaks above the
backend.

---

## 3. Consuming it (engine + scene)

Input flows into scenes two ways:

- `update(dt, input)` — the **fixed-timestep** logic step now receives the input,
  so movement is frame-rate independent *and* deterministic.
- `Context.input` — also available in `render()` for things tied to drawing (a
  crosshair at the mouse).

```cpp
void update(double dt, const platform::InputState& in) override {
    using K = platform::Key;
    float dx = 0, dy = 0;
    if (in.down(K::Left)  || in.down(K::A)) dx -= 1;   // LEVEL: continuous move
    if (in.down(K::Right) || in.down(K::D)) dx += 1;
    px_ += dx * speed * float(dt);                      // speed in px/second
}
```

> **Subtle timing note.** `update` may run 0, 1, or several times per rendered
> frame (the accumulator). `down` (a level) is perfectly safe to read there.
> *Edges* (`pressed`/`released`) are computed per *frame*, so if you need rock-solid
> one-shot handling, read edges in `render`, or (for M1 chess) handle clicks where
> there's exactly one update per frame. For M0's continuous movement, reading
> `down` in `update` is exactly right.

---

## 4. The demo

`InputDemoScene` moves the amber sprite with arrows/WASD (continuous, via `down`),
draws a white crosshair at the live mouse position, flashes a red marker while the
left button is held, and prints a HUD with the mouse and sprite coordinates. ESC
quits (still wired as a backend kill-switch too).

---

## 5. Run & observe

```sh
cmake --build build
./build/demo
```

- **Arrows or WASD** glide the amber sprite around; it stops at the screen edges.
- The **white crosshair** tracks your mouse exactly (in framebuffer space).
- **Hold left mouse** → a red square appears at the cursor.
- The **HUD** shows live `mouse` and `pos` numbers; **ESC** quits.

(Head-less `HAND_ENGINE_FRAMES=60 ./build/demo` still exits 0 — there's just no one
to press keys.)

---

## 6. Common pitfalls

- **`down` vs `pressed`.** Continuous → `down`; one-shot → `pressed`. The #1 input
  bug.
- **Edges from events instead of polled state.** Event delivery can drop frames
  under sdl2-compat/HiDPI; polling the level and diffing against last frame is more
  robust (and needs no auto-repeat filtering, since there are no key events).
- **Forgetting to store `down` for next frame** → the `was` comparison breaks and
  edges fire every frame. The level write *is* the bookkeeping.
- **Mouse in window vs framebuffer coords** → crosshair drifts from the cursor at
  non-1× scale. Map by the window/framebuffer ratio once, store framebuffer coords.
- **Reading SDL directly in a scene** → breaks the seam (and the web port). Always
  go through `Context.input` / `platform::input()`.

---

## 7. Glossary

- **Event vs state** — the raw event stream vs polling the current state each frame.
- **Polling + diffing** — read the level now (`SDL_GetKeyboardState`/`GetMouseState`),
  derive edges by comparing to the value stored last frame.
- **Level vs edge** — `down` (held) vs `pressed`/`released` (transition this frame).
- **Framebuffer coordinates** — the mouse mapped from window pixels by the
  window/framebuffer size ratio, so hits match what we draw.

---

## 8. Exercises

1. **One-shot toggle.** Use `in.pressed(Key::Space)` to toggle a boolean each
   press, and tint the background while it's on. Confirm one tap = one toggle (try
   it with `down` to feel the bug). *(Hint: read the edge.)*
2. **Diagonal speed.** Move diagonally — notice it's slightly faster than straight
   (the vector is longer). Normalize `(dx,dy)` to fix it. *(Hint: `math::normalize`
   on a `vec2`.)*
3. **Click to teleport.** On `in.pressed(MouseButton::Left)`, set the sprite
   position to the mouse position. *(Hint: read the edge in `render`, or store the
   click and apply in `update`.)*
4. **Add a key.** Add a row to the scancode table (`kmap`) in `backend_sdl.cpp`
   wiring some `Key` to its `SDL_SCANCODE_*`, and use its `pressed` edge to reset
   the sprite to center. Which file did you touch, and which didn't you?

---

## 9. What's next

The engine now sees the player. **Chapter 07** bakes two more web-portability
**seams** we'll need later but keep minimal now: the **asset** loader (file I/O via
an abstraction, for the web's virtual filesystem) and the **audio** init stub.
