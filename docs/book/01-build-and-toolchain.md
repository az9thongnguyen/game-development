# Chapter 01 — Build & Toolchain

> **Goal of this chapter.** Understand *exactly* what happens between the C++ text
> you write and a window appearing on screen, and set up a build you can trust for
> the rest of the project. By the end you'll run a tiny program that proves your
> machine can compile, link, and run SDL2 — and you'll understand every line that
> made it happen.
>
> **Prerequisites checklist**
> - [ ] A C++ compiler (Apple clang or Homebrew clang — already on most Macs)
> - [ ] CMake ≥ 3.20  (`brew install cmake`)
> - [ ] SDL2  (`brew install sdl2`)
> - [ ] A terminal open at the project root

---

## 1. The big picture: from text to a running program

It's tempting to treat "build" as a magic button. But three quarters of build
problems evaporate once you hold the right mental model, so let's build it.

Your `.cpp` files are plain text. Turning them into a program that runs is a
**pipeline** with four stages:

```
   main.cpp ─┐
             │  (1) PREPROCESS   #include pasted in, #define expanded
             ▼
   one big "translation unit"
             │  (2) COMPILE      C++  →  machine code, per file → .o object file
             ▼
   main.cpp.o   +   (SDL2 library)
             │  (3) LINK         glue all .o files + libraries into one executable
             ▼
   ./build/demo  (an executable file)
             │  (4) LOAD & RUN   OS loads it, finds SDL2 at runtime, calls main()
             ▼
   program runs
```

A few terms that make the rest of the project clearer:

- **Translation unit.** One `.cpp` *after* its `#include`s are pasted in. The
  compiler works on one translation unit at a time and emits one **object file**
  (`.o`) of machine code for it.
- **Header (`.hpp`) vs source (`.cpp`).** A header usually holds *declarations*
  ("a function named `init` exists, takes a `Config`, returns `bool`") — a
  promise. A source file holds the *definition* — the actual body. You `#include`
  a header to make a promise visible to many `.cpp` files; the definition lives in
  exactly one `.cpp`. (This is why `platform.hpp` describes the platform and
  `backend_sdl.cpp` implements it.)
- **Linking.** Compiling `main.cpp` that *calls* `SDL_Init` only records "I need a
  function called `SDL_Init`". The **linker** later finds the actual `SDL_Init`
  code inside the SDL2 library and wires the call to it. If linking fails with
  "undefined symbol", it means a *declaration* was visible but the *definition*
  (the library) wasn't supplied. That's the single most common build error, and
  now you know what it means.

> **Why this matters for us.** Our whole architecture (Chapter 02) is "engine code
> sees only `platform.hpp` declarations; only `backend_sdl.cpp` contains SDL
> definitions". The header/source split *is* the seam.

---

## 2. Why SDL2 — and the "thin shim" rule

A game needs the operating system for four boring-but-unavoidable things:

| Need | Without SDL | With SDL |
|------|-------------|----------|
| A **window** + a way to show pixels | hundreds of lines of Cocoa/Win32/X11 | a few calls |
| **Keyboard / mouse** input | per-OS event APIs | one event queue |
| **Audio** output | per-OS audio APIs | one audio callback |
| **Time** / vsync | per-OS timers | two functions |

Every one of those is platform plumbing we'd have to rewrite per OS — and none of
it is what we're here to learn. So we let SDL2 do *exactly* those four jobs, and
we hand-write everything interesting (drawing, math, game logic, AI).

**The rule we hold ourselves to:** SDL may (a) open a window, (b) hand us a buffer
of pixels and push it to the screen, (c) give us raw input, (d) play raw audio,
(e) tell us the time. We never call SDL's higher-level drawing functions
(`SDL_RenderDrawLine`, `SDL_image`, etc.). *If a triangle appears on screen, we
rasterized it.* Keeping SDL on a short leash is what makes the learning real and,
later, what makes swapping in a browser backend tractable.

---

## 3. Why CMake (and the two-phase mental model)

For one file, `clang++ main.cpp -o demo $(pkg-config --cflags --libs sdl2)` works.
It stops scaling the moment we have many files, several build configurations
(debug, sanitizers, release), and — crucially — a *second platform* (the browser,
at M5). **CMake** is a build-system *generator*: you describe *what* to build once,
and it generates the actual build files for whatever platform/compiler you're on.

CMake works in **two phases**, and conflating them causes confusion:

1. **Configure** (`cmake -B build ...`): CMake reads `CMakeLists.txt`, probes your
   compiler, *finds* SDL2, and writes a build system into `build/` plus a
   **cache** (`build/CMakeCache.txt`) remembering what it found.
2. **Build** (`cmake --build build`): runs that generated build system to actually
   compile + link. Re-running this only rebuilds what changed.

We always build **out-of-source** (everything generated lives in `build/`, which
is `.gitignore`d). That keeps the source tree clean and lets you blow away `build/`
to start fresh anytime.

> Edit `CMakeLists.txt` and just run `cmake --build build` again — CMake notices
> the change and re-configures automatically. You only re-run `cmake -B build` by
> hand when you want to change an *option* (like turning sanitizers on).

---

## 4. Reading our `CMakeLists.txt`, block by block

**Language standard.**
```cmake
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # error out instead of silently using older C++
set(CMAKE_CXX_EXTENSIONS OFF)         # standard C++20, not GNU dialect
```
We picked modern C++ over plain C mainly so vector/matrix math in later chapters
reads like math (`a + b`, `mat * v`) via operator overloading. We still hand-write
everything; the STL is plumbing, never a "game framework".

**Build type.**
```cmake
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()
```
A **Debug** build keeps debug symbols and skips optimization, so crashes give
readable stack traces and a debugger can step line-by-line. A **Release** build
optimizes hard (`-O2/-O3`) and strips assertions — faster, but harder to debug. We
default to Debug while learning.

**Strict warnings + optional sanitizers.**
```cmake
add_library(engine_flags INTERFACE)
target_compile_options(engine_flags INTERFACE -Wall -Wextra -Wpedantic)
if(ENGINE_SANITIZE)
  target_compile_options(engine_flags INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(engine_flags INTERFACE -fsanitize=address,undefined)
endif()
```
An **INTERFACE library** carries no code — it's a bundle of settings. Anything that
"links" it inherits those flags, so we configure strictness *once*. The two
sanitizers (opt-in via `-DENGINE_SANITIZE=ON`) are bug-finding superpowers:

- **AddressSanitizer (ASan)** catches memory errors at runtime: reading/writing
  out of bounds, use-after-free, double-free. For a *software renderer* that
  writes pixels by index, an off-by-one walks off the framebuffer — ASan turns
  that silent corruption into a precise, immediate report.
- **UndefinedBehaviorSanitizer (UBSan)** catches signed overflow, bad shifts,
  misaligned access, and similar landmines.

**Finding SDL2 (the fiddly part on macOS).**
```cmake
find_package(SDL2 CONFIG QUIET)        # prefer SDL2's own CMake package
if(SDL2_FOUND)
  set(SDL2_LINK SDL2::SDL2)
else()
  find_package(PkgConfig REQUIRED)     # fall back to pkg-config
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  set(SDL2_LINK PkgConfig::SDL2)
endif()
```
A library has to tell us two things: where its **headers** are (so `#include
<SDL.h>` resolves) and where its **compiled library** is (so the linker finds
`SDL_Init`). There are two standard ways to discover that:

- **CMake config package** — a `SDL2Config.cmake` file SDL ships. `find_package`
  loads it and gives you an **imported target** `SDL2::SDL2` that already knows the
  include dirs, library path, and any required compile flags. Linking that one
  target ("usage requirements") pulls all of it in transitively. This is the
  modern, preferred path.
- **pkg-config** — an older mechanism: a `sdl2.pc` text file listing the same
  flags. `pkg_check_modules(... IMPORTED_TARGET sdl2)` wraps it as
  `PkgConfig::SDL2`.

We try the config package first and fall back to pkg-config, so the build works no
matter how SDL2 got installed. Just above this, on macOS we ask `brew --prefix
sdl2` and add it to the search path, because Homebrew tucks libraries into a
versioned "keg" directory CMake wouldn't otherwise look in. **None of this leaks
above the platform layer** — it's all confined to how the `demo` target is linked.

> 💡 On this machine, `brew install sdl2` actually installed **sdl2-compat**: the
> SDL2 API re-implemented on top of SDL3. It provides the full SDL2 headers,
> library, and CMake/pkg-config files, so our code is identical — we just happen to
> be running on a newer engine underneath. (You'll see `.../opt/sdl2-compat/...` in
> the configure output.)

**The executable.**
```cmake
add_executable(demo src/main.cpp)
target_include_directories(demo PRIVATE src)        # so "engine/app.hpp" resolves
target_link_libraries(demo PRIVATE ${SDL2_LINK} engine_flags)
```
`target_include_directories(... src)` is why we can write `#include
"platform/platform.hpp"` from anywhere — paths are relative to `src/`. We keep the
source list **explicit** (we add files by hand as the engine grows) rather than
globbing: globbing looks convenient but CMake can't detect a newly added file
without a manual re-configure, which produces baffling "why isn't my file
compiling?" moments.

---

## 5. Reading `src/main.cpp` (this step's version)

This first version doesn't open a window — it answers one question: *can I build
and run an SDL2 program at all?* It prints two versions and then initializes SDL:

```cpp
SDL_version compiled;  SDL_VERSION(&compiled);     // what the HEADERS say (compile time)
SDL_version linked;    SDL_GetVersion(&linked);    // what the LIBRARY is  (run time)
if (SDL_Init(SDL_INIT_VIDEO) != 0) { ...SDL_GetError()...; return 1; }
SDL_Quit();
```

- The **compile-time** vs **run-time** version distinction is the header/library
  split from §1 made concrete: headers and the actual `.dylib` are separate things
  and *can* mismatch (they won't here, but the habit of checking is good).
- `SDL_Init(SDL_INIT_VIDEO)` proves SDL really *starts*, not merely links.
- `#include <SDL.h>` (not `<SDL2/SDL.h>`): the SDL2 target adds its own include
  directory, so the headers are found directly.
- SDL functions report failure by return value; the human-readable reason is in
  `SDL_GetError()`. We'll keep printing that on every failure path.

---

## 6. The build pipeline in practice

Run these from the project root:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug   # (1) configure → writes build/
cmake --build build                       # (2) build → compiles + links
./build/demo                              # (3) run
```

What appears in `build/` after configure: `CMakeCache.txt` (remembered choices),
`compile_commands.json` (see §7), and the generated build files. After building:
`build/CMakeFiles/demo.dir/src/main.cpp.o` (the object file) and `build/demo` (the
executable). Map those back onto the §1 diagram — object file = stage 2 output,
executable = stage 3 output.

Expected program output:

```
Hand-written engine — Step 1: toolchain check
  SDL2 headers (compile-time): 2.x.y
  SDL2 library (run-time):     2.x.y
  SDL_Init(SDL_INIT_VIDEO): OK
Toolchain looks good. On to Step 2: open a window.
```

The exact numbers don't matter — the win is the line `SDL_Init(...): OK`.

---

## 7. Editor setup (worth 5 minutes)

We set `CMAKE_EXPORT_COMPILE_COMMANDS ON`, so configure writes
`build/compile_commands.json` — a list of the exact compiler command for each
file. Point **clangd** (VS Code "clangd" extension, or any LSP editor) at it and
you get accurate autocomplete, go-to-definition, and red squiggles that match the
real build. Without it, your editor guesses at include paths and shows phantom
errors on `#include <SDL.h>` even though the build is fine.

---

## 8. Troubleshooting (with causes)

| Symptom | Likely cause & fix |
|---|---|
| `Could NOT find SDL2` at configure | SDL2 not installed → `brew install sdl2`, then re-run `cmake -B build`. Verify with `brew --prefix sdl2` and that `.../lib/pkgconfig/sdl2.pc` exists. |
| `'SDL.h' file not found` *in the editor* but build works | Editor doesn't know include paths → point clangd at `build/compile_commands.json`. |
| `'SDL.h' file not found` *during build* | Configure didn't find SDL2's include dir → re-configure; check the `SDL2: found via ...` line printed at configure time. |
| Linker error: `undefined symbol: _SDL_Init` | Headers found but library not linked (§1) → ensure `target_link_libraries(... ${SDL2_LINK})` is present and `SDL2_LINK` got set. |
| `undefined symbol: _main` / `_SDL_main` | Something redefined `main` → we avoid this with `SDL_MAIN_HANDLED` in the backend (Chapter 02). |
| Changes don't take effect | You edited a file but ran the old binary → re-run `cmake --build build` before `./build/demo`. |

---

## 9. Glossary

- **Compiler** — turns one translation unit into an object file.
- **Linker** — combines object files + libraries into an executable.
- **Object file (`.o`)** — machine code for one source file, not yet runnable.
- **Library** — precompiled code (here, SDL2) you link against.
- **Translation unit** — a `.cpp` after preprocessing.
- **Declaration vs definition** — "this exists" vs "here's its body".
- **Configure vs build** — CMake reading your project vs running the compile.
- **Imported target / usage requirements** — a CMake handle (`SDL2::SDL2`) that
  bundles include dirs + libs + flags so linking it "just works".
- **Sanitizer** — a compiler instrumentation that catches bugs at runtime.

---

## 10. Exercises

1. **Read a real error.** Change `#include <SDL.h>` to `#include <SDL_nope.h>`,
   build, and read the message. Which pipeline stage (§1) failed? Fix it.
   *(Hint: the file is missing before any code is compiled — that's preprocessing.)*
2. **Make a linker error.** Add a *declaration* with no definition near the top of
   `main.cpp`: `int mystery_number();` then `return mystery_number();` in `main`.
   Build. Note that compiling *succeeds* but linking *fails* — exactly the
   declaration-without-definition case. Remove it.
3. **Find your SDL.** Re-run `cmake -B build` and read the `SDL2: found via ...`
   line. Did your machine use the CMake config package or pkg-config?
4. **Turn on a sanitizer.** Configure a second build dir:
   `cmake -B build-asan -DENGINE_SANITIZE=ON && cmake --build build-asan`. It still
   prints `OK` — but from now on this build will scream if our renderer ever writes
   out of bounds. *(Hint: keep this build around; we'll use it at M0 acceptance.)*

---

## 11. What's next

We can build, link, and run. In **Chapter 02** we open a real window, allocate our
own **framebuffer** (the array of pixels we own), fill it by hand, and push it to
the screen through SDL — the canvas every later renderer writes into. It's also
where we set up the **platform seam**, the most important architectural idea in the
project.
