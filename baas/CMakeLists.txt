# =============================================================================
#  baas/CMakeLists.txt  —  the Game BaaS backend (Slice #1)
# =============================================================================
#  A SEPARATE process (requirements.md §11). Links Drogon (and, from S1.3,
#  libsodium) — but links NONE of the engine/game code. The engine core and the
#  games never depend on Drogon; only this target does. `engine_flags` carries
#  our -Wall/-Wextra/-Wpedantic; Drogon's headers arrive via an imported target
#  (treated as SYSTEM), so their warnings do not pollute our build.
# =============================================================================
# The root CMakeLists already runs find_package(Drogon) before add_subdirectory().
# Drogon's Find-modules create imported targets (UUID::/SQLite3::/Brotli::) with
# add_library(), which errors if invoked twice — so only find it if not present
# (this also lets `baas/` be configured standalone).
if(NOT TARGET Drogon::Drogon)
  find_package(Drogon CONFIG REQUIRED)
endif()

add_executable(baas
  main.cc
)
target_include_directories(baas PRIVATE ${CMAKE_SOURCE_DIR})   # so "baas/..." headers resolve
target_link_libraries(baas PRIVATE Drogon::Drogon engine_flags)
