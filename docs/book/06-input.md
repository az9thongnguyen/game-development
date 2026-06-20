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
*before* the frame callback. The pattern that makes the edges correct:

```cpp
// 1. New frame: clear the EDGES, keep the LEVELS.
for each key:  pressed = false;  released = false;   // down is left as-is

// 2. Drain events, updating both:
on KEYDOWN (not auto-repeat):  down = true;  pressed = true;
on KEYUP:                      down = false; released = true;
```

Clearing `pressed`/`released` each frame but **not** `down` is the whole trick:
`down` persists across frames until a key-up arrives, while the edges are true for
exactly the one frame the transition happened.

Two backend details worth noting:

- **Auto-repeat.** Holding a key makes SDL send repeated `KEYDOWN`s. We ignore
  those (`if (e.key.repeat) break;`) so `pressed` is a true edge, not a stutter.
- **Mouse in logical coordinates.** SDL reports the mouse in *window* pixels, but
  we draw in the 480×270 *framebuffer*. `SDL_RenderWindowToLogical` converts, so
  `mouse_x/mouse_y` line up with what you draw — essential later for clicking a
  chess square.

The game reaches it through `platform::input()` (and, more conveniently, through
the `Context`). `Key`/`InputState` deliberately live in the platform layer so this
all respects the one-way dependency rule.

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
- **Forgetting to clear edges** each frame → `pressed` stays true forever.
- **Counting auto-repeat as presses** → one held key reads as many presses.
- **Mouse in window vs logical coords** → crosshair drifts from the cursor at
  non-1× scale. Convert once, store logical.
- **Reading SDL directly in a scene** → breaks the seam (and the web port). Always
  go through `Context.input` / `platform::input()`.

---

## 7. Glossary

- **Event vs state** — the raw stream vs the per-frame snapshot.
- **Level vs edge** — `down` (held) vs `pressed`/`released` (transition this frame).
- **Auto-repeat** — the OS re-sending key-down while a key is held.
- **Logical coordinates** — framebuffer space (post window→logical conversion).

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
4. **Add a key.** Wire `Key::Enter` through `map_key` (already there) to reset the
   sprite to center. Which file did you touch, and which didn't you?

---

## 9. What's next

The engine now sees the player. **Chapter 07** bakes two more web-portability
**seams** we'll need later but keep minimal now: the **asset** loader (file I/O via
an abstraction, for the web's virtual filesystem) and the **audio** init stub.
