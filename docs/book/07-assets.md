# Chapter 07 — Asset & Audio Seams

> **Goal of this chapter.** Bake two more **web-portability seams** now, while they
> cost almost nothing, so M5 (the web port) and M2 (audio) drop in cleanly later:
> an **asset loader** that funnels all file I/O through one function, and an
> **audio init** stub that lives in the platform layer. We keep both deliberately
> minimal — this chapter is about *interfaces and architecture*, not features.

---

## 1. Why "seams" now instead of later

A *seam* is a clean boundary you build before you need it, so a future change
slots in without surgery. Two future changes in this project demand seams today:

- **Web filesystem (M5).** Browsers have no normal disk. Emscripten gives you a
  *virtual* filesystem, populated by preloading files at build time. If file
  reads are scattered as `fopen`/`ifstream` all over the code, adapting to that is
  a hunt-and-patch nightmare. If they all go through **one** function, M5 touches
  one file.
- **Audio (M2).** Real sound (gun/footstep effects in the FPS) needs the OS audio
  device — which is SDL's job, so it belongs in the platform layer behind the
  seam, not sprinkled into game code.

Building the seams now means M0 stays "web-ready from day one", exactly as the
requirements demand, without us implementing web or audio yet.

---

## 2. The asset loader

`assets::load_file(path)` reads an entire file into a byte vector:

```cpp
namespace assets {
    void set_base_path(const std::string& base);                 // where assets live
    std::optional<std::vector<uint8_t>> load_file(const std::string& path);
}
```

Three design choices worth understanding:

1. **Standard C++ I/O, not SDL.** We use `std::ifstream`. That keeps SDL confined
   to the platform backend (the seam rule from Chapter 02) — and it *still* works
   on the web, because Emscripten exposes its preloaded virtual files through the
   ordinary file API. So the same code path serves desktop and browser. (SDL's
   `RWops` would also work, but it would drag SDL into the engine for no benefit.)

2. **`std::optional` for "might not be there".** Loading can fail (typo, missing
   file, web file not preloaded). Returning `std::optional<...>` makes the failure
   impossible to ignore — the caller must check — instead of returning a null
   pointer or throwing. The demo uses this to fall back gracefully to a message
   instead of crashing:

   ```cpp
   if (auto data = assets::load_file("hello.txt")) { /* use *data */ }
   else { /* show a friendly "not found" line */ }
   ```

3. **A base path.** `set_base_path("assets")` lets the rest of the code say
   `load_file("hello.txt")` without repeating the directory. At M5 this becomes
   wherever the preloaded files are mounted.

We exercise it for real: `assets/hello.txt` is loaded at startup and its first
line is drawn in green at the bottom of the demo — proof the pipeline works end to
end. (Loading is best-effort, so even if you run from the wrong directory the demo
just shows a "not found" note and keeps running.)

> **Pitfall — current directory.** `load_file` resolves paths relative to the
> process's *working directory*, not the executable. Run the demo from the project
> root (`./build/demo`) so `assets/` is found. Production builds usually copy
> assets next to the binary or embed them; for M0, "run from the root" is fine and
> documented.

---

## 3. The audio seam

Audio needs the OS sound device, which is SDL territory, so the seam lives in the
**platform interface**:

```cpp
bool init_audio();   // platform.hpp — implemented in backend_sdl.cpp
```

At M0 it is a deliberate **stub** (`return true;`). There is no sound in the M0
acceptance demo, and opening a real device buys us nothing yet — but the *seam*
now exists, in the right layer, so M2 implements actual mixing/playback behind it
without changing any architecture. `main` calls `platform::init_audio()` at
startup to show where it goes.

> Putting audio in the platform layer (not an `engine/audio` module) is a small
> but real architecture decision: like input, audio is *normalized hardware*, and
> keeping it behind `platform.hpp` preserves the one-way dependency rule and the
> "swap backend for web" property.

---

## 4. Run & observe

```sh
cmake --build build
./build/demo            # run from the project root so assets/ resolves
```

Same input demo as Chapter 06, plus a **green line at the bottom** reading
"Hello from assets/ …" — the contents of `assets/hello.txt`, loaded through the
asset seam. If you run it from a different directory you'll instead see the
"(not found)" fallback, demonstrating the `std::optional` error path.

```sh
HAND_ENGINE_FRAMES=30 ./build/demo   # head-less; exits 0
```

---

## 5. Common pitfalls

- **Scattered file I/O.** Any `ifstream`/`fopen` outside `assets` re-opens the
  M5 problem. Always go through `assets::load_file`.
- **Ignoring load failure.** The `std::optional` is there to be checked — don't
  `*value` it blindly.
- **Working-directory assumptions.** Resolve from a known base; document how to run.
- **Audio creeping into game code.** Keep it behind `platform::`; game code should
  ask the engine to play a sound, never poke SDL audio.

---

## 6. Glossary

- **Seam** — a clean boundary built ahead of a known future change.
- **Virtual filesystem (VFS)** — Emscripten's in-memory files, served via the
  normal file API.
- **`std::optional<T>`** — "a `T`, or nothing" — forces explicit failure handling.
- **Base path** — the directory relative paths resolve against.
- **Stub** — a placeholder implementation that satisfies the interface for now.

---

## 7. Exercises

1. **Load and count.** Print the byte size of `assets/hello.txt` from
   `load_file(...)->size()`. Then add a second file and load it too.
2. **Force the error path.** Run `./build/demo` from inside `build/` and watch the
   "(not found)" fallback appear — then fix it by running from the root. Why did
   the path resolution change?
3. **A tiny config.** Make a `assets/config.txt` with a number, load it, parse it
   (e.g. `std::stoi`), and use it as the sprite speed. *(Hint: bytes → string →
   int.)*
4. **Predict M2.** Sketch what `platform::play_sound(...)` would need as parameters
   for the FPS's gunshot. (You don't have to implement it — just design the seam.)

---

## 8. What's next

All the engine subsystems exist. **Chapter 08** assembles the real **M0 demo
scene** — moving shapes, an alpha sprite, text, and a live **FPS counter** — then
runs the **acceptance checks**: build clean, no leaks (sanitizers + the `leaks`
tool), and the architecture invariants (no SDL above the platform, no blocking
loop). That's the M0 finish line.
