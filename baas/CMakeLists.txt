# =============================================================================
#  baas/CMakeLists.txt  —  the Game BaaS backend (Slice #1)
# =============================================================================
#  A SEPARATE process (requirements.md §11). Links Drogon + libsodium — but links
#  NONE of the engine/game code. The engine core and games never depend on Drogon;
#  only these targets do. `engine_flags` carries our -Wall/-Wextra/-Wpedantic;
#  Drogon's headers arrive via an imported target (SYSTEM), so their warnings do
#  not pollute our build.
#
#  Layout: all server code lives in the `baas_core` static library so the
#  integration tests can link it WITHOUT main.cc (they provide their own main()).
#  The `baas` executable is just main.cc + baas_core.
# =============================================================================

# The root CMakeLists already runs find_package(Drogon) before add_subdirectory().
# Drogon's Find-modules create imported targets (UUID::/SQLite3::/Brotli::) with
# add_library(), which errors if invoked twice — so only find it if not present
# (this also lets `baas/` be configured standalone).
if(NOT TARGET Drogon::Drogon)
  find_package(Drogon CONFIG REQUIRED)
endif()

# ---- server library (everything except the entry point) ----------------------
# OBJECT (not STATIC) is deliberate: Drogon filters/controllers self-register via
# static initializers, and a static-archive member with no referenced symbol is
# dropped by the linker — so the registration never runs. An OBJECT library
# compiles every .o straight into each target, guaranteeing registration.
add_library(baas_core OBJECT
  db/db.cc
  common/errors.cc
  gateway/api_key_filter.cc
  app_setup.cc
)
target_include_directories(baas_core PUBLIC ${CMAKE_SOURCE_DIR})   # so "baas/..." headers resolve
target_link_libraries(baas_core PUBLIC Drogon::Drogon PRIVATE engine_flags)

# ---- the server executable ---------------------------------------------------
add_executable(baas main.cc)
target_link_libraries(baas PRIVATE baas_core engine_flags)

# ---- integration tests (need Drogon, so they live here, not in the root) -----
# libcurl drives real HTTP against the app booted on an ephemeral port.
find_package(CURL REQUIRED)

add_executable(test_baas_auth ${CMAKE_SOURCE_DIR}/tests/test_baas_auth.cc)
target_link_libraries(test_baas_auth PRIVATE baas_core CURL::libcurl engine_flags)
add_test(NAME baas_auth COMMAND test_baas_auth)
