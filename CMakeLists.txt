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
# The SDL2 desktop 'demo' is the ONLY target that needs SDL2. Turn it OFF to build a
# backend-only tree (the BaaS + SDL-free *_core libs/tests) without SDL2 installed —
# e.g. inside a server container. Configure never requires SDL2 when this is OFF.
option(ENGINE_BUILD_DESKTOP "Build the SDL2 desktop 'demo' executable" ON)

# ---- SDL2 (web build, M5: staged + untested) ---------------------------------
# On the web, SDL2 is provided by Emscripten's port via a compile/link flag, NOT
# find_package. This branch keeps the structure web-ready; it is exercised at M5.
if(EMSCRIPTEN)
  # SDL2 comes from Emscripten's port (NOT find_package); the framebuffer is
  # pushed to a <canvas>; assets are preloaded into the virtual filesystem; the
  # output is a self-contained .html using our shell (which sets Module.arguments).
  add_compile_options(-sUSE_SDL=2)
  # -lidbfs.js links the IndexedDB filesystem. Without it `FS.mount(IDBFS, ...)` in the
  # page throws "IDBFS is not defined" and every save lives and dies in the tab — which
  # is exactly what happened, and was only visible because the page says so out loud
  # instead of failing quietly.
  add_link_options(-sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -lidbfs.js
                   "--preload-file=${CMAKE_SOURCE_DIR}/assets@assets"
                   # ...but NOT the parts of assets/ that are this machine's state.
                   # saves/, releases/ and channels/ are gitignored for exactly that
                   # reason, and the packager did not know it: the published bundle was
                   # shipping the developer's device id (so every browser signed in as
                   # the same guest), their save files, their channel pointers and the
                   # release audit log. Found by loading the page and reading the id
                   # back out of it.
                   "--exclude-file=${CMAKE_SOURCE_DIR}/assets/saves"
                   "--exclude-file=${CMAKE_SOURCE_DIR}/assets/releases"
                   "--exclude-file=${CMAKE_SOURCE_DIR}/assets/channels"
                   "--shell-file=${CMAKE_SOURCE_DIR}/web/shell.html")
  set(CMAKE_EXECUTABLE_SUFFIX ".html")   # emcc emits demo.html + demo.js + demo.wasm
  set(SDL2_LINK "")
elseif(NOT ENGINE_BUILD_DESKTOP)
  # Backend-only build: no desktop 'demo', so SDL2 is not needed and not searched for.
  set(SDL2_LINK "")
  message(STATUS "ENGINE_BUILD_DESKTOP=OFF — skipping SDL2 + the desktop 'demo' target")
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
# Built for the desktop (needs SDL2) or the web (Emscripten's SDL port). A backend-only
# build (ENGINE_BUILD_DESKTOP=OFF, non-web) skips it — see the option above.
if(EMSCRIPTEN OR ENGINE_BUILD_DESKTOP)
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
  src/games/fx/fx_scene.cpp
  src/games/light/light_scene.cpp
  src/games/audio/audio_scene.cpp
  src/games/anim/anim_scene.cpp
  src/games/hub/hub_panel.cpp
  src/games/farm/farm_scene.cpp
  src/games/studio_shell/studio_shell_scene.cpp
  src/games/studio_shell/map_workspace.cpp
  src/games/studio_shell/palette.cpp
  src/games/studio_shell/project_panel.cpp
  src/games/studio_shell/play_viewport.cpp
  src/games/studio_shell/scene_workspace.cpp
  src/games/studio_shell/pixel_workspace.cpp
  src/games/studio_shell/workspace_host.cpp
  src/games/runner/worker.cpp
)
target_include_directories(demo PRIVATE src)
if(EMSCRIPTEN)
  # The page is baked into demo.html at LINK time by --shell-file, and CMake does not
  # know that. Editing web/shell.html therefore rebuilt nothing and silently shipped
  # the previous page — found the hard way, by testing a change that was never in the
  # bundle. LINK_DEPENDS makes the shell a real input.
  set_property(TARGET demo APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html")

  # The collection page is a SEPARATE page, so it cannot read the preloaded MEMFS that
  # lives inside the wasm module — it fetches over HTTP like any page. Put next to
  # demo.html the four things it asks for: itself, the baked index, the covers, and the
  # READMEs beside the manifests.
  #
  # An ALLOWLIST of asset subdirectories, not the tree minus a few. The preload learned
  # this the expensive way in chapter 128: `assets/` also holds saves/, releases/ and
  # channels/, which are this machine's state, and shipping saves/device.id gave every
  # browser the same guest account. A denylist is one forgotten line away from doing it
  # again; a list of what MAY be served cannot leak what nobody added to it.
  #
  # A custom TARGET, not a POST_BUILD step on demo. POST_BUILD only runs when demo
  # relinks, so editing just the page copied nothing and the browser check went on
  # testing the previous one — which is the same trap `--shell-file` set above, and it
  # was sprung again within the hour of writing this. A custom target is always
  # out of date, so the four copies always happen; they are half a megabyte.
  add_custom_target(web_pages
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/web/collection.html" "$<TARGET_FILE_DIR:demo>/collection.html"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/assets/collection.json" "$<TARGET_FILE_DIR:demo>/collection.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/textures" "$<TARGET_FILE_DIR:demo>/assets/textures"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets/projects" "$<TARGET_FILE_DIR:demo>/assets/projects"
    VERBATIM)
  add_dependencies(demo web_pages)
endif()
target_link_libraries(demo PRIVATE ${SDL2_LINK} engine_flags chess_core fps_core farm_core map_edit_core paint_core commands_core document_core render3d_core viz3d_core ui_core text_core physics_core colony_core mem_core gbaas_sdk studio_core particles_core tween_core light_core audio_core runner_core project_core inspect_core resource_core release_core release_ops_core hub_core hub_build_core)
# (runner_core PUBLIC-links sandbox_core, so demo gets it transitively — no duplicate)
# (colony_core PUBLIC-links iso_core/ecs_core/jobs_core, so demo gets them transitively)
endif()  # EMSCRIPTEN OR ENGINE_BUILD_DESKTOP

# ---- Tests (CTest) -----------------------------------------------------------
# Dependency-free unit tests for the engine core (no SDL needed). Run with:
#   ctest --test-dir build --output-on-failure
enable_testing()

# ---- DEFLATE (subsystem: reading a foreign format) ---------------------------
# Hand-written RFC 1951/1950 decompression, so a PNG can be read without a third
# party. Decompress only: nothing here writes one, and half a codec with a consumer
# beats a whole one without.
add_library(inflate_core STATIC src/engine/compress/inflate.cpp)
target_include_directories(inflate_core PUBLIC src)
target_link_libraries(inflate_core PRIVATE engine_flags)

add_executable(test_inflate tests/test_inflate.cpp)
target_link_libraries(test_inflate PRIVATE inflate_core engine_flags)
add_test(NAME inflate COMMAND test_inflate)

# ---- PNG (reading a format we did not invent) --------------------------------
# Decode only, on top of inflate_core. `.hrt` stays the format the engine ships;
# this is the door that lets an open-licence art pack in through `asset.import`.
add_library(png_core STATIC src/engine/image_png.cpp src/engine/image.cpp)
target_include_directories(png_core PUBLIC src)
target_link_libraries(png_core PUBLIC inflate_core PRIVATE engine_flags)

add_executable(test_png tests/test_png.cpp src/engine/assets.cpp)
target_compile_definitions(test_png PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_png PRIVATE png_core engine_flags)
add_test(NAME png COMMAND test_png)

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

# Golden: drive the whole Studio shell headless (no window) and check its structure.
# A Scene needs only an engine::Context, and Renderer2D writes into any framebuffer the
# caller owns — so a "window" screen is unit-testable like anything else.
add_executable(test_shell_golden tests/test_shell_golden.cpp
  src/games/studio_shell/studio_shell_scene.cpp
  src/games/studio_shell/map_workspace.cpp
  src/games/studio_shell/palette.cpp
  src/games/farm/farm_scene.cpp
  src/engine/image.cpp
  src/engine/anim/flipbook.cpp
  src/games/studio_shell/project_panel.cpp
  src/games/studio_shell/play_viewport.cpp
  src/games/studio_shell/scene_workspace.cpp
  src/games/studio_shell/pixel_workspace.cpp
  src/games/studio_shell/workspace_host.cpp
  src/games/hub/hub_panel.cpp
  src/engine/renderer2d.cpp
  src/engine/assets.cpp
)
target_include_directories(test_shell_golden PRIVATE src)
target_compile_definitions(test_shell_golden PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_shell_golden PRIVATE
  hub_build_core hub_core release_ops_core release_core project_core inspect_core resource_core
  commands_core map_edit_core paint_core document_core ui_core text_core farm_core sandbox_core ecs_core gbaas_sdk engine_flags)
add_test(NAME shell_golden COMMAND test_shell_golden)

# ---- Documents: undo/redo history and autosave+recovery for an edited file -------
# Named `document`, not `studio`: studio_core is already the Texture Lab.
add_library(document_core STATIC
  src/engine/document/command_stack.cpp
  src/engine/document/document.cpp
  src/engine/document/save.cpp
)
target_include_directories(document_core PUBLIC src)
target_link_libraries(document_core PRIVATE engine_flags)

add_executable(test_document tests/test_document.cpp
  src/engine/document/command_stack.cpp
  src/engine/document/document.cpp
  src/engine/document/save.cpp
  src/engine/assets.cpp
)
target_include_directories(test_document PRIVATE src)
target_link_libraries(test_document PRIVATE engine_flags)
add_test(NAME document COMMAND test_document)

# ---- Commands: every operation, named once and callable from anywhere ------------
# The registry itself is pure. release_commands binds the platform-spine ops to ids,
# so --cmd, a palette and a button all reach the same release_ops function.
add_library(commands_core STATIC
  src/engine/commands/all_commands.cpp
  src/engine/commands/asset_commands.cpp
  src/engine/commands/registry.cpp
  src/engine/commands/release_commands.cpp
)
target_include_directories(commands_core PUBLIC src)
# project_core is PUBLIC because asset.new writes a manifest line: creating an asset
# and declaring it are one act, and splitting them would give the Studio a way to make
# a file the project cannot see.
target_link_libraries(commands_core PUBLIC release_ops_core png_core studio_core paint_core provenance_core project_core tilemap_core PRIVATE engine_flags)

# Compiles the spine sources directly (like test_release_ops) so the test binary owns
# a complete assets:: implementation without linking a window or a backend.
add_executable(test_commands tests/test_commands.cpp
  src/engine/commands/registry.cpp
  src/engine/commands/release_commands.cpp
  src/engine/release/ops.cpp
  src/engine/project/inspect.cpp
  src/engine/release/release.cpp
  src/engine/project/project.cpp
  src/engine/resource/resource.cpp
  src/engine/assets.cpp
  src/engine/commands/asset_commands.cpp
  src/engine/project/collection.cpp
  src/engine/asset/provenance.cpp
  src/engine/image.cpp
  src/engine/tilemap/map2.cpp
)
target_include_directories(test_commands PRIVATE src)
target_compile_definitions(test_commands PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_commands PRIVATE png_core studio_core paint_core engine_flags)
add_test(NAME commands COMMAND test_commands)

# ---- Paint: pixel edits as undoable commands --------------------------------------
# The third client of doc::CommandStack, whose header has named a pixel editor since
# chapter 113. Same shape as map_edit_core on purpose: an edit returns a doc::Command
# instead of mutating and hoping somebody records it.
# PURE: an Image in, commands out. No renderer, no I/O, no UI.
add_library(paint_core STATIC
  src/engine/paint/colour.cpp
  src/engine/paint/paint.cpp
  src/engine/paint/pixel_source.cpp
)
target_include_directories(paint_core PUBLIC src)
target_link_libraries(paint_core PUBLIC document_core PRIVATE engine_flags)

add_executable(test_paint tests/test_paint.cpp
  src/engine/image.cpp
  src/engine/assets.cpp     # image.cpp's load_image goes through the I/O seam
)
target_include_directories(test_paint PRIVATE src)
target_link_libraries(test_paint PRIVATE paint_core document_core engine_flags)
add_test(NAME paint COMMAND test_paint)

# ---- Tilemap: the shared 2D map format, follow camera and autotile rule -----------
# PURE: text in, structs out; no renderer, no I/O. Replaces two incompatible private
# grids (fps::Map, iso::TileMap) with one format that has layers, collision, entities
# and triggers — the things a second 2D game cannot be authored without.
add_library(tilemap_core STATIC
  src/engine/tilemap/tileset.cpp
  src/engine/tilemap/map2.cpp
  src/engine/tilemap/camera2d.cpp
  src/engine/tilemap/autotile.cpp
)
target_include_directories(tilemap_core PUBLIC src)
target_link_libraries(tilemap_core PRIVATE engine_flags)

add_executable(test_tilemap tests/test_tilemap.cpp
  src/engine/image.cpp src/engine/assets.cpp)
target_include_directories(test_tilemap PRIVATE src)
target_compile_definitions(test_tilemap PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_tilemap PRIVATE tilemap_core engine_flags)
add_test(NAME tilemap COMMAND test_tilemap)

# ---- Map edits: the joint between the map format and the undo stack ---------------
# Its own library rather than a third file in tilemap_core, because it is the ONLY
# piece that needs both: tilemap_core stays free of document_core, and a game that
# only reads maps does not link an undo stack.
add_library(map_edit_core STATIC src/engine/tilemap/map_edit.cpp)
target_include_directories(map_edit_core PUBLIC src)
target_link_libraries(map_edit_core PUBLIC tilemap_core document_core PRIVATE engine_flags)

add_executable(test_map_edit tests/test_map_edit.cpp)
target_include_directories(test_map_edit PRIVATE src)
target_link_libraries(test_map_edit PRIVATE map_edit_core engine_flags)
add_test(NAME map_edit COMMAND test_map_edit)

# The Studio's map workspace, driven with no window: a Renderer2D writes into any
# framebuffer and a platform::InputState is a plain struct, so a drag, an undo, a save
# and a recovery prompt are all unit-testable.
# The pixel editor, driven headless. Same harness as test_map_workspace, one
# workspace later — and the round trip matters more here: an editor that writes bytes
# the engine cannot decode loses the work silently.
add_executable(test_pixel_workspace tests/test_pixel_workspace.cpp
  src/games/studio_shell/pixel_workspace.cpp
  src/engine/renderer2d.cpp
  src/engine/assets.cpp
  src/engine/image.cpp
)
target_include_directories(test_pixel_workspace PRIVATE src)
target_compile_definitions(test_pixel_workspace PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_pixel_workspace PRIVATE
  paint_core commands_core document_core ui_core text_core engine_flags)
add_test(NAME pixel_workspace COMMAND test_pixel_workspace)

add_executable(test_map_workspace tests/test_map_workspace.cpp
  src/games/studio_shell/map_workspace.cpp
  src/engine/renderer2d.cpp
  src/engine/assets.cpp
)
target_include_directories(test_map_workspace PRIVATE src)
target_link_libraries(test_map_workspace PRIVATE
  map_edit_core commands_core document_core ui_core text_core fps_core engine_flags)
add_test(NAME map_workspace COMMAND test_map_workspace)

# ---- Farm: the second game's simulation, with no renderer in it ------------------
# The day loop, the crops, the NPC schedules and the save format are all pure, so a
# test can play three days and check the parsnips came up when the definition says.
add_library(farm_core STATIC
  src/games/farm/defs.cpp
  src/games/farm/theme.cpp
  src/games/farm/controls.cpp
  src/games/farm/world.cpp
  src/games/farm/dialogue.cpp
  src/games/farm/cloud.cpp
)
target_include_directories(farm_core PUBLIC src)
target_link_libraries(farm_core PUBLIC tilemap_core document_core PRIVATE engine_flags)

# Also compiles image.cpp + assets.cpp and links studio_core: the theme test now has a
# PROVENANCE half that regenerates the pond tile from its committed .recipe and compares
# bytes, which needs the generator, the .hrt encoder and the asset root.
add_executable(test_farm tests/test_farm.cpp
  src/engine/image.cpp
  src/engine/assets.cpp
)
target_include_directories(test_farm PRIVATE src)
target_compile_definitions(test_farm PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_farm PRIVATE farm_core studio_core engine_flags)
add_test(NAME farm COMMAND test_farm)

# The farm PLAYED, with no window: press the keys, check the world changed, and check
# the screen changed with it — a simulation the render ignores looks like a frozen game.
add_executable(test_farm_scene tests/test_farm_scene.cpp
  src/engine/image.cpp
  src/games/farm/farm_scene.cpp
  src/engine/renderer2d.cpp
  src/engine/assets.cpp
)
target_include_directories(test_farm_scene PRIVATE src)
target_compile_definitions(test_farm_scene PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_farm_scene PRIVATE farm_core ui_core text_core gbaas_sdk engine_flags)
add_test(NAME farm_scene COMMAND test_farm_scene)

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
# noise -> texture_gen -> recipe. Testable headless; the `--lab texture` scene links it.
add_library(studio_core STATIC
  src/games/studio/noise.cpp
  src/games/studio/texture_gen.cpp
  src/games/studio/recipe.cpp
  src/games/studio/sheet.cpp
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


# ---- Particle system (Track A: engine depth) — PURE deterministic sim (no SDL) ----
# Seeded xorshift32 emission + integration; drawing is scene-side over Renderer2D.
add_library(particles_core STATIC
  src/engine/fx/particles.cpp
)
target_include_directories(particles_core PUBLIC src)
target_link_libraries(particles_core PRIVATE engine_flags)

add_executable(test_particles tests/test_particles.cpp
  src/engine/fx/particles.cpp
)
target_include_directories(test_particles PRIVATE src)
target_link_libraries(test_particles PRIVATE engine_flags)
add_test(NAME particles COMMAND test_particles)

# ---- Tween / easing (Track A: engine depth) — PURE deterministic animation ----
# Named easing curves + a scalar Tween; the interpolation primitive camera/UI/
# particles/sprite-animation all build on. No SDL, unit-tested headless.
add_library(tween_core STATIC
  src/engine/anim/tween.cpp
  src/engine/anim/flipbook.cpp
)
target_include_directories(tween_core PUBLIC src)
target_link_libraries(tween_core PRIVATE engine_flags)

add_executable(test_tween tests/test_tween.cpp
  src/engine/anim/tween.cpp
)
target_include_directories(test_tween PRIVATE src)
target_link_libraries(test_tween PRIVATE engine_flags)
add_test(NAME tween COMMAND test_tween)

# The game.project manifest: the versioned root (Horizon 0 platform spine) that lets
# a build be selected by a project file instead of a hard-coded CLI branch. Pure,
# headless, unit-tested — no SDL, no assets.
add_library(project_core STATIC
  src/engine/project/project.cpp
)
target_include_directories(project_core PUBLIC src)
target_link_libraries(project_core PRIVATE engine_flags)

# One project resolve, as data (inspect.hpp): read + validate + hash every declared
# asset. Four copies of this existed — CLI launch, CLI inspect, publish, hub — and they
# had drifted. Impure (reads through assets::), so its symbols come from the final link.
add_library(inspect_core STATIC
  src/engine/project/inspect.cpp
  # The collection index (chapter 130): one directory of manifests -> one file a page
  # can read. Same lib because it is the same act — read + validate — applied to many.
  src/engine/project/collection.cpp
)
target_include_directories(inspect_core PUBLIC src)
target_link_libraries(inspect_core PRIVATE engine_flags)

add_executable(test_inspect tests/test_inspect.cpp
  src/engine/project/inspect.cpp
  src/engine/project/project.cpp
  src/engine/resource/resource.cpp
  src/engine/assets.cpp
)
target_include_directories(test_inspect PRIVATE src)
target_link_libraries(test_inspect PRIVATE engine_flags)
add_test(NAME inspect COMMAND test_inspect)

# The fixed-timestep accumulator, extracted from App so the Play viewport can share
# it instead of writing a second one. Header-only and pure — the test just includes it.
add_executable(test_fixed_step tests/test_fixed_step.cpp)
target_include_directories(test_fixed_step PRIVATE src)
target_link_libraries(test_fixed_step PRIVATE engine_flags)
add_test(NAME fixed_step COMMAND test_fixed_step)

add_executable(test_project tests/test_project.cpp
  src/engine/project/project.cpp
)
target_include_directories(test_project PRIVATE src)
target_link_libraries(test_project PRIVATE engine_flags)
add_test(NAME project COMMAND test_project)

# The collection index. Two halves, two kinds of check: to_json is pure (escaping,
# field order, stable bytes), and build_collection reads the REAL projects/ directory —
# which is how the committed assets/collection.json is held to being what the manifests
# bake to, and how a project added without re-baking becomes a failing test rather than
# a game missing from the page.
# ---- Provenance: where every picture in this repo came from ------------------
# The rule "every new .hrt gains an ATTRIBUTION line" had no enforcement and had
# already been forgotten twenty times. This lib derives the answer from the marks the
# three doors leave on disk, so forgetting is a red test rather than a silent hole.
# assets.cpp is NOT listed here: the final target that links this provides it, the
# same arrangement inspect_core/resource_core use.
add_library(provenance_core STATIC src/engine/asset/provenance.cpp)
target_include_directories(provenance_core PUBLIC src)
target_link_libraries(provenance_core PRIVATE engine_flags)

add_executable(test_provenance tests/test_provenance.cpp
  src/engine/asset/provenance.cpp
  src/engine/assets.cpp
)
target_include_directories(test_provenance PRIVATE src)
target_compile_definitions(test_provenance PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets")
target_link_libraries(test_provenance PRIVATE engine_flags)
add_test(NAME provenance COMMAND test_provenance)

add_executable(test_collection tests/test_collection.cpp
  src/engine/project/collection.cpp
  src/engine/project/inspect.cpp
  src/engine/project/project.cpp
  src/engine/resource/resource.cpp
  src/engine/assets.cpp
)
target_include_directories(test_collection PRIVATE src)
target_compile_definitions(test_collection PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets")
target_link_libraries(test_collection PRIVATE engine_flags)
add_test(NAME collection COMMAND test_collection)

# Content hashing (Horizon 0 resource identity): the deterministic fingerprint a
# package manifest / preview-parity check needs. Pure, headless, unit-tested.
add_library(resource_core STATIC
  src/engine/resource/resource.cpp
)
target_include_directories(resource_core PUBLIC src)
target_link_libraries(resource_core PRIVATE engine_flags)

add_executable(test_resource tests/test_resource.cpp
  src/engine/resource/resource.cpp
)
target_include_directories(test_resource PRIVATE src)
target_link_libraries(test_resource PRIVATE engine_flags)
add_test(NAME resource COMMAND test_resource)

# Local immutable release store (Horizon 1): content-addressed release paths +
# movable channel pointers (preview/production) for promote/rollback. Pure paths &
# validators; the I/O lives in main.cpp behind the assets:: seam. Headless, unit-tested.
add_library(release_core STATIC
  src/engine/release/release.cpp
)
target_include_directories(release_core PUBLIC src)
target_link_libraries(release_core PRIVATE engine_flags)

add_executable(test_release tests/test_release.cpp
  src/engine/release/release.cpp
)
target_include_directories(test_release PRIVATE src)
target_link_libraries(test_release PRIVATE engine_flags)
add_test(NAME release COMMAND test_release)

# Release operations (publish/promote/rollback) as callable functions returning a
# structured result — shared by the CLI and the graphical Hub Scene. Reads/writes through
# assets:: (symbols provided by the final demo link). Tested end-to-end against a temp base.
add_library(release_ops_core STATIC
  src/engine/release/ops.cpp
)
target_include_directories(release_ops_core PUBLIC src)
target_link_libraries(release_ops_core PRIVATE engine_flags)

add_executable(test_release_ops tests/test_release_ops.cpp
  src/engine/release/ops.cpp
  src/engine/project/inspect.cpp
  src/engine/release/release.cpp
  src/engine/project/project.cpp
  src/engine/resource/resource.cpp
  src/engine/assets.cpp
)
target_include_directories(test_release_ops PRIVATE src)
target_link_libraries(test_release_ops PRIVATE engine_flags)
add_test(NAME release_ops COMMAND test_release_ops)

# Hub shell view model (Horizon 1): the "next recommended action" decision over a
# project's aggregate project+release state. Pure/headless; the graphical Scene is later.
add_library(hub_core STATIC
  src/engine/hub/hub.cpp
)
target_include_directories(hub_core PUBLIC src)
target_link_libraries(hub_core PRIVATE engine_flags)

add_executable(test_hub tests/test_hub.cpp
  src/engine/hub/hub.cpp
)
target_include_directories(test_hub PRIVATE src)
target_link_libraries(test_hub PRIVATE engine_flags)
add_test(NAME hub COMMAND test_hub)

# Hub view assembler (impure): reads a project + release store through the assets:: seam
# into a HubView, shared by the CLI (--hub) and the Studio's Hub section. Its
# assets:: symbols are provided by the final demo target (like renderer3d/Renderer2D).
add_library(hub_build_core STATIC
  src/engine/hub/hub_build.cpp
)
target_include_directories(hub_build_core PUBLIC src)
# Only needs the headers to compile; the project/resource/release/hub + assets:: symbols
# are provided by the final demo link (which links those cores directly).
target_link_libraries(hub_build_core PRIVATE engine_flags)

add_executable(test_flipbook tests/test_flipbook.cpp
  src/engine/anim/flipbook.cpp
)
target_include_directories(test_flipbook PRIVATE src)
target_link_libraries(test_flipbook PRIVATE engine_flags)
add_test(NAME flipbook COMMAND test_flipbook)

# ---- 2D lighting (Track A: engine depth) — PURE radial-light math (no SDL) ----
# Falloff + additive per-pixel contribution; the framebuffer deposit is scene-side
# over Renderer2D::add_pixel. Unit-tested headless.
add_library(light_core STATIC
  src/engine/fx/light.cpp
)
target_include_directories(light_core PUBLIC src)
target_link_libraries(light_core PRIVATE engine_flags)

add_executable(test_light tests/test_light.cpp
  src/engine/fx/light.cpp
)
target_include_directories(test_light PRIVATE src)
target_link_libraries(test_light PRIVATE engine_flags)
add_test(NAME light COMMAND test_light)

# ---- Audio mixer (Track A: engine depth) — PURE software voice mixer (no SDL) ----
# Sums active voices into one clipped chunk a scene streams to platform::play_sound;
# fixes the queue-only seam where overlapping clips couldn't play together.
add_library(audio_core STATIC
  src/engine/audio/mixer.cpp
)
target_include_directories(audio_core PUBLIC src)
target_link_libraries(audio_core PRIVATE engine_flags)

add_executable(test_audio tests/test_audio.cpp
  src/engine/audio/mixer.cpp
)
target_include_directories(test_audio PRIVATE src)
target_link_libraries(test_audio PRIVATE engine_flags)
add_test(NAME audio COMMAND test_audio)

# ---- Test-runner worker core (Track C): runs a sandbox scenario headlessly ----
# Pure (no SDK/no Drogon): reuses the sandbox sim to execute + check a scenario.
add_library(runner_core STATIC
  src/games/runner/runner.cpp
)
target_include_directories(runner_core PUBLIC src)
target_link_libraries(runner_core PUBLIC sandbox_core PRIVATE engine_flags)

# test_runner compiles the sandbox sources directly (dependency-free, like test_sandbox).
add_executable(test_runner tests/test_runner.cpp
  src/games/runner/runner.cpp
  src/games/sandbox/world.cpp
  src/games/sandbox/serialize.cpp
  src/engine/ecs/registry.cpp
)
target_include_directories(test_runner PRIVATE src)
target_link_libraries(test_runner PRIVATE engine_flags)
add_test(NAME runner COMMAND test_runner)

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
target_link_libraries(fps_core PUBLIC tilemap_core PRIVATE engine_flags)

add_executable(test_fps tests/test_fps.cpp)
target_compile_definitions(test_fps PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_fps PRIVATE fps_core tilemap_core engine_flags)
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
