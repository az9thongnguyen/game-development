# =============================================================================
#  Hand-written C++ game engine — root build script
# =============================================================================
#  One CMake project builds the desktop (native + SDL2) target today, and is
#  structured so the web (Emscripten) target can be added at M5 WITHOUT touching
#  engine or game code (see cmake/emscripten.toolchain.cmake).
#
#  The source list grows step-by-step as we add engine modules. Keeping the list
#  EXPLICIT (instead of file globbing) is deliberate: it is clearer for learning,
#  and CMake re-globbing pitfalls are avoided.
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(hand_engine LANGUAGES CXX)

# ---- Language standard -------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)            # modern C++; operator overloading for math
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # fail rather than silently downgrade
set(CMAKE_CXX_EXTENSIONS OFF)         # plain C++20, no compiler-specific dialect

# Default to a Debug build (with symbols + assertions) when the user did not pick.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Export compile_commands.json so editors/clangd give good completion + diagnostics.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Options -----------------------------------------------------------------
option(ENGINE_SANITIZE "Enable Address + UndefinedBehavior sanitizers (dev)" OFF)

# ---- SDL2 (web build, M5: staged + untested) ---------------------------------
# On the web, SDL2 is provided by Emscripten's port via a compile/link flag, NOT
# find_package. This branch keeps the structure web-ready; it is exercised at M5.
if(EMSCRIPTEN)
  add_compile_options(-sUSE_SDL=2)
  add_link_options(-sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1
                   "--preload-file=${CMAKE_SOURCE_DIR}/assets@assets")
  set(SDL2_LINK "")
endif()

# ---- Find SDL2 (desktop platform backend) ------------------------------------
# Strategy: prefer SDL2's own CMake config package; fall back to pkg-config.
# On macOS, Homebrew installs into a keg (e.g. .../opt/sdl2*), so we point CMake
# at the brew prefix first. None of this leaks above the platform layer.
if(NOT EMSCRIPTEN AND APPLE)
  find_program(BREW_EXECUTABLE brew)
  if(BREW_EXECUTABLE)
    execute_process(
      COMMAND ${BREW_EXECUTABLE} --prefix sdl2
      OUTPUT_VARIABLE SDL2_BREW_PREFIX
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(SDL2_BREW_PREFIX)
      list(PREPEND CMAKE_PREFIX_PATH "${SDL2_BREW_PREFIX}")
    endif()
  endif()
endif()

find_package(SDL2 CONFIG QUIET)
if(SDL2_FOUND)
  set(SDL2_LINK SDL2::SDL2)
  message(STATUS "SDL2: found via CMake config (${SDL2_DIR})")
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
  set(SDL2_LINK PkgConfig::SDL2)
  message(STATUS "SDL2: found via pkg-config (version ${SDL2_VERSION})")
endif()

# ---- Shared compile flags ----------------------------------------------------
# An INTERFACE target carries warnings (and optional sanitizers) to anything that
# links it, so every module is built with the same strict settings.
add_library(engine_flags INTERFACE)
target_compile_options(engine_flags INTERFACE -Wall -Wextra -Wpedantic)
if(ENGINE_SANITIZE)
  target_compile_options(engine_flags INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(engine_flags INTERFACE -fsanitize=address,undefined)
endif()

# ---- Executable (sources added as the engine grows) --------------------------
add_executable(demo
  src/main.cpp
  src/platform/backend_sdl.cpp
  src/engine/app.cpp
  src/engine/renderer2d.cpp
  src/engine/assets.cpp
  src/engine/image.cpp
  src/demo/demo_scene.cpp
  src/games/chess/chess_tui.cpp
  src/games/chess/chess_gui.cpp
  src/games/fps/raycast_scene.cpp
)
target_include_directories(demo PRIVATE src)
target_link_libraries(demo PRIVATE ${SDL2_LINK} engine_flags chess_core fps_core)

# ---- Tests (CTest) -----------------------------------------------------------
# Dependency-free unit tests for the engine core (no SDL needed). Run with:
#   ctest --test-dir build --output-on-failure
enable_testing()

add_executable(test_math tests/test_math.cpp)
target_include_directories(test_math PRIVATE src)
target_link_libraries(test_math PRIVATE engine_flags)
add_test(NAME math COMMAND test_math)

# ---- 3D core (M3): reusable software renderer3d + geometry + cameras ---------
# Pure of SDL. renderer3d.cpp references Renderer2D, whose symbols are provided
# by whichever final target links it (the demo exe, or the test below).
add_library(render3d_core STATIC
  src/engine/geometry.cpp
  src/engine/renderer3d.cpp
  src/engine/camera.cpp
)
target_include_directories(render3d_core PUBLIC src)
target_link_libraries(render3d_core PRIVATE engine_flags)

# ---- 3D pipeline + geometry + rasterizer tests (M3): no SDL ------------------
# The rasterizer test drives a real (heap-allocated) framebuffer through
# Renderer2D, so it compiles renderer2d.cpp directly — no window needed.
add_executable(test_render3d tests/test_render3d.cpp src/engine/renderer2d.cpp)
target_include_directories(test_render3d PRIVATE src)
target_link_libraries(test_render3d PRIVATE render3d_core engine_flags)
add_test(NAME render3d COMMAND test_render3d)

# ---- Chess core (M1): UI-agnostic library, shared by tests + frontends --------
# Plain C++ — NO engine/SDL dependency (engine_flags only carries warning flags).
add_library(chess_core STATIC
  src/games/chess/board.cpp
  src/games/chess/fen.cpp
  src/games/chess/movegen.cpp
  src/games/chess/game.cpp
  src/games/chess/evaluate.cpp
  src/games/chess/search.cpp
)
target_include_directories(chess_core PUBLIC src)
target_link_libraries(chess_core PRIVATE engine_flags)

add_executable(test_chess tests/test_chess.cpp)
target_link_libraries(test_chess PRIVATE chess_core engine_flags)
add_test(NAME chess COMMAND test_chess)

# ---- FPS core (M2): pure raycast/grid logic, testable without SDL --------------
add_library(fps_core STATIC
  src/games/fps/map.cpp
  src/games/fps/raycast.cpp
  src/games/fps/textures.cpp
)
target_include_directories(fps_core PUBLIC src)
target_link_libraries(fps_core PRIVATE engine_flags)

add_executable(test_fps tests/test_fps.cpp)
target_link_libraries(test_fps PRIVATE fps_core engine_flags)
add_test(NAME fps COMMAND test_fps)
