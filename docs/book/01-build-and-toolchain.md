# Chapter 01 — Build & Toolchain

> **What you'll learn:** how the project is compiled, why we use CMake + SDL2 the
> way we do, and how to confirm your machine can build and run an SDL2 program.
> By the end you'll run a tiny program that prints the SDL2 version and proves the
> toolchain works.

---

## 1. The idea

Before we write a single pixel of our own renderer, we need a reliable way to:

1. **Compile** modern C++ into a native executable.
2. **Link** against SDL2 — our one and only external library.
3. **Run** the result and see output.

That's the entire job of this chapter. It feels unglamorous, but a solid build
setup is what lets every later chapter "just work". We also make a decision here
that pays off all the way at M5 (the web port): we use **CMake**, which can drive
*both* a native build and an Emscripten/WebAssembly build from the same files.

### Why SDL2, and why "only a thin shim"?

A game ultimately needs the operating system to do four boring-but-essential
things:

- open a **window** and let us put a rectangle of pixels on the screen,
- give us **keyboard/mouse** input,
- play **audio**,
- tell us what **time** it is (for animation/timing).

Doing those four things portably across macOS, Windows, Linux, and the browser is
a huge amount of platform-specific code. SDL2 does exactly that, and nothing we
care about learning. So we let SDL2 handle the *boring* parts, and we hand-write
the *interesting* parts (drawing, math, game logic, AI).

The rule we hold ourselves to: **SDL2 is only allowed to (a) create a window,
(b) hand us a buffer of pixels and push it to the screen, (c) give us raw input,
(d) play raw audio, (e) tell us the time.** We never call SDL's higher-level
drawing functions. If a triangle appears on screen, *we* rasterized it.

### Why CMake?

`clang++ main.cpp -o demo ...` works for one file, but breaks down as soon as we
have many files, multiple build configurations (debug, sanitizers, release), and
— crucially — a second platform (the browser). CMake is a build-system
*generator*: you describe *what* to build, and it produces the actual build files
for your platform. The same `CMakeLists.txt` will, at M5, build WebAssembly via
the Emscripten toolchain without us rewriting anything.

---

## 2. The code

Three files make up this step.

### `CMakeLists.txt` — the build description

Read it top to bottom; here are the parts that matter.

**Project + C++ standard.** We require C++20 and refuse to silently fall back:

```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
```

We chose modern C++ (not plain C) mainly because **operator overloading** makes
the vector/matrix math in later chapters read like math (`a + b`, `mat * v`)
instead of `vec3_add(a, b)`. We still hand-write everything; the STL is only used
for plumbing (containers, etc.), never as a "game framework".

**Finding SDL2.** This is the fiddly part on macOS, so we made it robust:

```cmake
find_package(SDL2 CONFIG QUIET)   # prefer SDL2's own CMake package
if(SDL2_FOUND)
  set(SDL2_LINK SDL2::SDL2)
else()
  find_package(PkgConfig REQUIRED) # fall back to pkg-config
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  set(SDL2_LINK PkgConfig::SDL2)
endif()
```

There are two common ways to locate a library: its **CMake config package**
(a `SDL2Config.cmake` file the library ships) and **pkg-config** (a `sdl2.pc`
file describing compile/link flags). We try the CMake package first and fall back
to pkg-config, so the build works regardless of how SDL2 got installed. On macOS,
Homebrew tucks libraries into a "keg" directory, so just before that we ask
`brew --prefix sdl2` and add it to the search path.

> 💡 On this machine `brew install sdl2` actually installed **sdl2-compat** — the
> SDL2 API implemented on top of SDL3. That's fine: it provides the full SDL2 API
> and the same CMake/pkg-config files, so nothing in our code changes.

**Linking it together.** `SDL2_LINK` now holds whichever target name worked, and
the `demo` executable links it:

```cmake
add_executable(demo src/main.cpp)
target_include_directories(demo PRIVATE src)
target_link_libraries(demo PRIVATE ${SDL2_LINK} engine_flags)
```

The `engine_flags` INTERFACE library is a small trick: it carries our warning
flags (`-Wall -Wextra -Wpedantic`) and optional sanitizers to every target that
links it, so we configure strictness once and reuse it everywhere.

> We keep the source list **explicit** (`src/main.cpp`, and we'll add more by
> hand) rather than using a glob. Globbing seems convenient but CMake can't tell
> when you add a new file, leading to confusing "why isn't my file building?"
> moments. Explicit is clearer for learning.

### `src/main.cpp` — the smoke test

This first version doesn't open a window yet. It answers one question: *can I
build and run an SDL2 program at all?* It prints two versions:

- **compile-time** version (`SDL_VERSION`) — what the headers say,
- **run-time** version (`SDL_GetVersion`) — what the actual linked library is,

and then calls `SDL_Init(SDL_INIT_VIDEO)` to confirm SDL can really start the
video subsystem (not just link). If init fails it prints `SDL_GetError()` and
returns non-zero. Note `#include <SDL.h>` — because the SDL2 target adds its own
include directory, we include `SDL.h` directly rather than `SDL2/SDL.h`.

### `.gitignore`

The `build/` directory is generated output — never commit it. The `.gitignore`
keeps the repository to just source + docs.

---

## 3. Run & observe

From the project root:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug   # configure (generates build/)
cmake --build build                       # compile + link
./build/demo                              # run
```

You should see something like:

```
Hand-written engine — Step 1: toolchain check
  SDL2 headers (compile-time): 2.x.y
  SDL2 library (run-time):     2.x.y
  SDL_Init(SDL_INIT_VIDEO): OK
Toolchain looks good. On to Step 2: open a window.
```

The exact `2.x.y` numbers don't matter — seeing the line `SDL_Init(...): OK` is
the win. The toolchain compiles, links, and runs SDL2.

> The first `cmake -B build` is the slow one (it probes your compiler and finds
> SDL2). After that, `cmake --build build` only recompiles what changed.

---

## 4. Troubleshooting

- **`Could NOT find SDL2`** — run `brew install sdl2`, then re-run the first
  `cmake -B build` command. If it still fails, `brew --prefix sdl2` should print a
  path; that path's `lib/pkgconfig/sdl2.pc` is what the pkg-config fallback uses.
- **`'SDL.h' file not found`** — your editor may not know the include path yet.
  The build itself uses CMake's flags; for editor completion, point your tooling
  at `build/compile_commands.json` (we enabled its generation).
- **Linker errors about `_main`** — make sure you didn't rename `int main(...)`.

---

## 5. Exercises

1. **Break it on purpose.** Change `#include <SDL.h>` to `#include <SDL_nope.h>`,
   rebuild, and read the error. Then fix it. (Getting comfortable reading build
   errors early pays off.)
2. **Force a failure.** Temporarily change `SDL_INIT_VIDEO` to an obviously
   invalid flag value and observe the `SDL_GetError()` message. Revert after.
3. **Inspect the config.** Run `cmake -B build` again and read the `SDL2: found
   via ...` status line it prints. Which path (CMake config or pkg-config) did
   your machine use?

---

## 6. What's next

We can build and run. In **Chapter 02** we open a real window, allocate our own
**framebuffer** (a big array of pixels we own), fill it by hand, and push it to
the screen through SDL — the foundation every later renderer writes into.
