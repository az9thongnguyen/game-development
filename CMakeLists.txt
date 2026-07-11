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
  # SDL2 comes from Emscripten's port (NOT find_package); the framebuffer is
  # pushed to a <canvas>; assets are preloaded into the virtual filesystem; the
  # output is a self-contained .html using our shell (which sets Module.arguments).
  add_compile_options(-sUSE_SDL=2)
  add_link_options(-sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1
                   "--preload-file=${CMAKE_SOURCE_DIR}/assets@assets"
                   "--shell-file=${CMAKE_SOURCE_DIR}/web/shell.html")
  set(CMAKE_EXECUTABLE_SUFFIX ".html")   # emcc emits demo.html + demo.js + demo.wasm
  set(SDL2_LINK "")
else()
  # ---- Find SDL2 (desktop platform backend) ----------------------------------
  # Strategy: prefer SDL2's own CMake config package; fall back to pkg-config.
  # On macOS, Homebrew installs into a keg (e.g. .../opt/sdl2*), so we point CMake
  # at the brew prefix first. None of this leaks above the platform layer.
  if(APPLE)
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
  src/engine/asset_cache.cpp
  src/engine/image.cpp
  src/demo/demo_scene.cpp
  src/games/chess/chess_tui.cpp
  src/games/chess/chess_gui.cpp
  src/games/fps/raycast_scene.cpp
  src/games/viz3d/scene3d.cpp
  src/games/viz3d/editor_scene.cpp
  src/games/iso/iso_render.cpp
  src/games/iso/iso_scene.cpp
  src/games/editor/editor_scene.cpp
  src/games/colony/colony_scene.cpp
  src/games/studio/studio_scene.cpp
  src/games/sandbox/sandbox_scene.cpp
  src/games/maplab/maplab_scene.cpp
)
target_include_directories(demo PRIVATE src)
target_link_libraries(demo PRIVATE ${SDL2_LINK} engine_flags chess_core fps_core render3d_core viz3d_core ui_core text_core physics_core colony_core mem_core gbaas_sdk studio_core sandbox_core maplab_core)
# (colony_core PUBLIC-links iso_core/ecs_core/jobs_core, so demo gets them transitively)

# ---- Tests (CTest) -----------------------------------------------------------
# Dependency-free unit tests for the engine core (no SDL needed). Run with:
#   ctest --test-dir build --output-on-failure
enable_testing()

add_executable(test_math tests/test_math.cpp)
target_include_directories(test_math PRIVATE src)
target_link_libraries(test_math PRIVATE engine_flags)
add_test(NAME math COMMAND test_math)

# ---- Memory allocators (subsystem A): reusable engine-core foundation ----------
# Five hand-written allocators (arena/stack/pool/freelist/frame). Pure, no SDL.
add_library(mem_core STATIC
  src/engine/memory/arena.cpp
  src/engine/memory/stack_alloc.cpp
  src/engine/memory/pool.cpp
  src/engine/memory/freelist.cpp
  src/engine/memory/frame.cpp
)
target_include_directories(mem_core PUBLIC src)
target_link_libraries(mem_core PRIVATE engine_flags)

add_executable(test_mem tests/test_mem.cpp)
target_include_directories(test_mem PRIVATE src)
target_link_libraries(test_mem PRIVATE mem_core engine_flags)
add_test(NAME mem COMMAND test_mem)

# ---- Engine-core ECS (subsystem B): generic type-erased sparse-set registry -----
add_library(ecs_core STATIC src/engine/ecs/registry.cpp)
target_include_directories(ecs_core PUBLIC src)
target_link_libraries(ecs_core PRIVATE engine_flags)

add_executable(test_ecs tests/test_ecs.cpp)
target_include_directories(test_ecs PRIVATE src)
target_link_libraries(test_ecs PRIVATE ecs_core engine_flags)
add_test(NAME ecs COMMAND test_ecs)

# ---- Job system (subsystem C): thread pool on desktop, synchronous on web ---------
find_package(Threads REQUIRED)
add_library(jobs_core STATIC src/engine/jobs/job_system.cpp)
target_include_directories(jobs_core PUBLIC src)
target_link_libraries(jobs_core PUBLIC Threads::Threads PRIVATE engine_flags)

add_executable(test_jobs tests/test_jobs.cpp)
target_include_directories(test_jobs PRIVATE src)
target_link_libraries(test_jobs PRIVATE jobs_core engine_flags)
add_test(NAME jobs COMMAND test_jobs)

# ---- Asset pipeline + hot reload (subsystem D): cache over the I/O seam -----------
# Compiles assets.cpp + asset_cache.cpp directly (matches the test_iso pattern); the
# round-trip uses a temp base path, so no SDL and no bundled fixtures are needed.
add_executable(test_assets tests/test_assets.cpp
  src/engine/assets.cpp
  src/engine/asset_cache.cpp
  src/engine/image.cpp
)
target_include_directories(test_assets PRIVATE src)
target_link_libraries(test_assets PRIVATE engine_flags)
add_test(NAME assets COMMAND test_assets)

# ---- 2D physics (subsystem E): rigid bodies, collision, impulse resolution --------
add_library(physics_core STATIC
  src/engine/physics/collision.cpp
  src/engine/physics/world.cpp
)
target_include_directories(physics_core PUBLIC src)
target_link_libraries(physics_core PRIVATE engine_flags)

add_executable(test_physics tests/test_physics.cpp)
target_include_directories(test_physics PRIVATE src)
target_link_libraries(test_physics PRIVATE physics_core engine_flags)
add_test(NAME physics COMMAND test_physics)

# ---- Text: anti-aliased fonts (stb_truetype) --------------------------------------
# PURE core: no I/O, no SDL. stb_truetype is confined to font.cpp (impl macro there),
# so `third_party` is a PRIVATE include — consumers just see engine/text/font.hpp.
add_library(text_core STATIC src/engine/text/font.cpp)
target_include_directories(text_core PUBLIC src PRIVATE third_party)
target_link_libraries(text_core PRIVATE engine_flags)

add_executable(test_font tests/test_font.cpp src/engine/assets.cpp src/engine/renderer2d.cpp)
target_include_directories(test_font PRIVATE src)
target_compile_definitions(test_font PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_font PRIVATE text_core engine_flags)
add_test(NAME font COMMAND test_font)

# Anti-aliasing: SSAA coordinate scaling + Wu lines + coverage shapes (no SDL).
add_executable(test_aa tests/test_aa.cpp src/engine/renderer2d.cpp)
target_include_directories(test_aa PRIVATE src)
target_link_libraries(test_aa PRIVATE text_core engine_flags)
add_test(NAME aa COMMAND test_aa)

# ---- Immediate-mode GUI (subsystem F): editor support -----------------------------
# Draws via Renderer2D (symbols provided by the linking target); logic runs headless.
add_library(ui_core STATIC src/engine/ui/ui.cpp)
target_include_directories(ui_core PUBLIC src)
target_link_libraries(ui_core PRIVATE engine_flags)

# renderer2d.cpp (compiled in here) uses the font module → link text_core.
add_executable(test_ui tests/test_ui.cpp src/engine/renderer2d.cpp)
target_include_directories(test_ui PRIVATE src)
target_link_libraries(test_ui PRIVATE ui_core text_core engine_flags)
add_test(NAME ui COMMAND test_ui)

# Design-system tokens: pure constants, no SDL/renderer needed.
add_executable(test_theme tests/test_theme.cpp)
target_include_directories(test_theme PRIVATE src)
target_link_libraries(test_theme PRIVATE engine_flags)
add_test(NAME theme COMMAND test_theme)

# Golden: render a representative UI offscreen (ss=2), check solid fills + AA + PPM.
add_executable(test_ui_golden tests/test_ui_golden.cpp src/engine/renderer2d.cpp src/engine/assets.cpp)
target_include_directories(test_ui_golden PRIVATE src)
target_compile_definitions(test_ui_golden PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_ui_golden PRIVATE ui_core text_core engine_flags)
add_test(NAME ui_golden COMMAND test_ui_golden)

# ---- Colony (engine-core integration game): USES ecs + jobs + iso A*/tilemap -------
# The "integration" deliverable — a game standing on the engine-core libs (B + C here;
# A + D + F are used by its scene). Reuses iso_core's A*/TileMap (generic algorithms).
add_library(colony_core STATIC src/games/colony/colony.cpp)
target_include_directories(colony_core PUBLIC src)
target_link_libraries(colony_core PUBLIC ecs_core jobs_core iso_core PRIVATE engine_flags)

add_executable(test_colony tests/test_colony.cpp)
target_include_directories(test_colony PRIVATE src)
target_link_libraries(test_colony PRIVATE colony_core engine_flags)
add_test(NAME colony COMMAND test_colony)

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
target_link_libraries(test_render3d PRIVATE render3d_core text_core engine_flags)
add_test(NAME render3d COMMAND test_render3d)

# ---- viz3d sandbox model (M3.5): pure editor logic, testable without SDL -----
add_library(viz3d_core STATIC
  src/games/viz3d/editor.cpp
)
target_include_directories(viz3d_core PUBLIC src)
target_link_libraries(viz3d_core PRIVATE engine_flags)

add_executable(test_viz3d tests/test_viz3d.cpp)
target_include_directories(test_viz3d PRIVATE src)
target_link_libraries(test_viz3d PRIVATE viz3d_core render3d_core engine_flags)
add_test(NAME viz3d COMMAND test_viz3d)

# ---- Mini Studio (Track B): procedural content authoring — PURE core (no SDL) ----
# noise -> texture_gen -> recipe. Testable headless; the --studio scene links it.
add_library(studio_core STATIC
  src/games/studio/noise.cpp
  src/games/studio/texture_gen.cpp
  src/games/studio/recipe.cpp
)
target_include_directories(studio_core PUBLIC src)
target_link_libraries(studio_core PRIVATE engine_flags)

# test_studio compiles image.cpp + assets.cpp directly so the encode/decode + recipe
# round-trips stay dependency-free (round-trips use in-memory buffers, no disk).
add_executable(test_studio tests/test_studio.cpp
  src/engine/image.cpp
  src/engine/assets.cpp
)
target_include_directories(test_studio PRIVATE src)
target_link_libraries(test_studio PRIVATE studio_core engine_flags)
add_test(NAME studio COMMAND test_studio)

# ---- Sandbox editor (Track B): declarative 2D playground — PURE core (no SDL) ----
# components + World::tick systems + text serializer. On the generic ecs::Registry.
add_library(sandbox_core STATIC
  src/games/sandbox/world.cpp
  src/games/sandbox/serialize.cpp
)
target_include_directories(sandbox_core PUBLIC src)
target_link_libraries(sandbox_core PRIVATE engine_flags)

# test_sandbox compiles registry.cpp directly (ecs::Registry's non-template ops) to
# stay dependency-free — the tick/serializer round-trips use only in-memory state.
add_executable(test_sandbox tests/test_sandbox.cpp
  src/games/sandbox/world.cpp
  src/games/sandbox/serialize.cpp
  src/engine/ecs/registry.cpp
)
target_include_directories(test_sandbox PRIVATE src)
target_link_libraries(test_sandbox PRIVATE engine_flags)
add_test(NAME sandbox COMMAND test_sandbox)

# ---- Map/Level Lab (Track B): tile-grid editor — PURE edit-op core (no SDL) ----
# set_cell / fill_rect / flood_fill on fps::Map (the one shared grid model).
add_library(maplab_core STATIC
  src/games/maplab/edit.cpp
)
target_include_directories(maplab_core PUBLIC src)
target_link_libraries(maplab_core PRIVATE engine_flags)

add_executable(test_maplab tests/test_maplab.cpp
  src/games/maplab/edit.cpp
)
target_include_directories(test_maplab PRIVATE src)
target_link_libraries(test_maplab PRIVATE engine_flags)
add_test(NAME maplab COMMAND test_maplab)

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

# ---- Iso sim core (M4): tile map + ECS + A* + farm model + serialize ---------
# Pure C++, SDL-free, so the simulation is unit-tested without a window. The
# renderer + scene (iso_render.cpp, iso_scene.cpp) link into the demo exe, not here.
add_library(iso_core STATIC
  src/games/iso/tilemap.cpp
  src/games/iso/pathfind.cpp
  src/games/iso/farm.cpp
  src/games/iso/serialize.cpp
)
target_include_directories(iso_core PUBLIC src)
target_link_libraries(iso_core PRIVATE engine_flags)

# test_iso also compiles assets.cpp directly so the serialize round-trip stays
# dependency-free (the round-trip itself uses in-memory buffers, no disk).
add_executable(test_iso tests/test_iso.cpp src/engine/assets.cpp)
target_include_directories(test_iso PRIVATE src)
target_link_libraries(test_iso PRIVATE iso_core engine_flags)
add_test(NAME iso COMMAND test_iso)

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

# ---- Native webserver (requirements.md §11): SEPARATE process, NOT engine core --
# Hand-written HTTP over POSIX sockets; serves the WASM bundle + a leaderboard API.
# Links NO engine/game code (engine_flags carries only warning flags). Desktop only:
# it is never part of the web build. Include root is the project root so the files
# resolve their own "server/..." headers.
if(NOT EMSCRIPTEN)
  add_executable(webserver
    server/main.cpp
    server/http.cpp
    server/static_files.cpp
    server/leaderboard.cpp
    server/net.cpp
  )
  target_include_directories(webserver PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(webserver PRIVATE engine_flags)
endif()

# test_server omits net.cpp on purpose, so the suite never opens a socket.
add_executable(test_server tests/test_server.cpp
  server/http.cpp
  server/static_files.cpp
  server/leaderboard.cpp
)
target_include_directories(test_server PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_server PRIVATE engine_flags)
add_test(NAME server COMMAND test_server)

# ---- Prefer a WebSocket-capable libcurl across the WHOLE build -----------------
# macOS's system libcurl lacks ws://; Homebrew's (keg-only) has it. If we let some
# targets find the system curl and others the Homebrew one, a test binary can end
# up linking BOTH — and the system curl then wins symbol resolution, so ws:// fails
# with "Unsupported protocol". Pinning CURL_ROOT here makes every find_package(CURL)
# resolve to the one ws-capable libcurl, so no binary links two. (Native, non-web.)
if(APPLE AND NOT EMSCRIPTEN AND EXISTS /opt/homebrew/opt/curl/include/curl/websockets.h)
  set(CURL_ROOT /opt/homebrew/opt/curl)
  list(PREPEND CMAKE_PREFIX_PATH /opt/homebrew/opt/curl)
endif()

# ---- Game BaaS C++ SDK (gbaas_sdk): game-facing client, native + web -----------
# NOT gated on Drogon: the SDK links no server code. Only libcurl (native) or
# emscripten_fetch (web). The colony game links this to talk to the backend.
add_subdirectory(sdk/cpp)

add_executable(test_sdk_client tests/test_sdk_client.cc)
target_link_libraries(test_sdk_client PRIVATE gbaas_sdk engine_flags)
add_test(NAME sdk_client COMMAND test_sdk_client)

# Realtime handle: driven by a FAKE WebSocket transport (no socket) — proves op
# framing + server-event parsing/dispatch deterministically.
add_executable(test_sdk_realtime tests/test_sdk_realtime.cc)
target_link_libraries(test_sdk_realtime PRIVATE gbaas_sdk engine_flags)
add_test(NAME sdk_realtime COMMAND test_sdk_realtime)

# ---- Game BaaS backend (Slice #1, requirements.md §11): SEPARATE process --------
# Drogon-based platform (auth + leaderboard + ...). Links NONE of the engine/game
# code — the engine core never depends on Drogon. Desktop only (never the web
# build). GUARDED on Drogon being installed so the ordinary engine build never
# hard-depends on it: run `brew install drogon libsodium` to enable the 'baas'
# target. On macOS the Homebrew prefix is added so find_package locates Drogon.
if(NOT EMSCRIPTEN)
  if(APPLE AND BREW_EXECUTABLE)
    execute_process(
      COMMAND ${BREW_EXECUTABLE} --prefix
      OUTPUT_VARIABLE HOMEBREW_PREFIX
      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(HOMEBREW_PREFIX)
      list(PREPEND CMAKE_PREFIX_PATH "${HOMEBREW_PREFIX}")
    endif()
  endif()
  find_package(Drogon CONFIG QUIET)
  if(Drogon_FOUND)
    message(STATUS "Drogon: found (${Drogon_DIR}) — building 'baas' backend")
    add_subdirectory(baas)
  else()
    message(STATUS "Drogon not found — skipping 'baas' backend (brew install drogon)")
  endif()
endif()
