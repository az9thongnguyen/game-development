# =============================================================================
#  cmake/emscripten.toolchain.cmake  —  WebAssembly build (M5; staged, not built)
# =============================================================================
#  This file makes the web build a DROP-IN at M5. It is intentionally NOT used or
#  verified yet — M0 only guarantees the structure is ready. Do not expect it to
#  work until the Emscripten SDK is installed and M5 is implemented.
#
#  How it will be used at M5 (after installing/activating emsdk):
#
#      emcmake cmake -B build-web
#      cmake --build build-web
#      # serve build-web/demo.html over http and open it
#
#  `emcmake` sets EMSCRIPTEN and points CMake at Emscripten's own toolchain; this
#  file layers our project-specific flags on top. The CMakeLists `if(EMSCRIPTEN)`
#  branch supplies SDL2 via `-sUSE_SDL=2` (NOT find_package) and preloads assets.
#
#  Notes for M5:
#   * SDL2 on web comes from Emscripten's port: compile + link with -sUSE_SDL=2.
#   * The software framebuffer is pushed to a <canvas> — the same present() path,
#     just a different backend; engine/game code does not change.
#   * The loop MUST use emscripten_set_main_loop (no blocking while) — already
#     guaranteed because the loop lives in platform::run (see Chapter 03).
#   * Assets are preloaded into the virtual filesystem (--preload-file assets@assets)
#     and read through engine/assets.cpp unchanged (see Chapter 07).
# =============================================================================

# When invoked via `emcmake`, Emscripten's toolchain is already configured and the
# EMSCRIPTEN variable is set. If someone points CMake here directly, chain to the
# real Emscripten toolchain from the active SDK.
if(NOT DEFINED ENV{EMSDK})
  message(FATAL_ERROR
    "Emscripten not found: set up emsdk (https://emscripten.org) and use "
    "`emcmake cmake ...` instead of this file directly. (M5 task.)")
endif()

include("$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")
