# =============================================================================
#  baas/CMakeLists.txt  —  the Game BaaS backend (Slice #1)
# =============================================================================
#  A SEPARATE process (requirements.md §11). Links Drogon + libsodium — but links
#  NONE of the engine/game code. The engine core and games never depend on Drogon;
#  only these targets do. `engine_flags` carries our -Wall/-Wextra/-Wpedantic;
#  Drogon's headers arrive via an imported target (SYSTEM), so their warnings do
#  not pollute our build.
#
#  Layout: all server code lives in the `baas_core` OBJECT library so the tests
#  can link it WITHOUT main.cc (they provide their own main()), and so Drogon
#  filters/controllers self-register (a STATIC archive would drop unreferenced
#  members and the registration would never run).
# =============================================================================

# The root CMakeLists already runs find_package(Drogon) before add_subdirectory().
# Drogon's Find-modules create imported targets (UUID::/SQLite3::/Brotli::) with
# add_library(), which errors if invoked twice — so only find it if not present.
if(NOT TARGET Drogon::Drogon)
  find_package(Drogon CONFIG REQUIRED)
endif()

# libsodium: argon2id password hashing + HMAC-SHA256 for JWT (via pkg-config).
find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)

# ---- server library (everything except the entry point) ----------------------
add_library(baas_core OBJECT
  app_config.cc
  db/db.cc
  common/errors.cc
  gateway/api_key_filter.cc
  gateway/auth_filter.cc
  gateway/rate_limiter.cc
  auth/password.cc
  auth/jwt.cc
  auth/auth_service.cc
  auth/auth_controller.cc
  leaderboard/lb_service.cc
  leaderboard/lb_controller.cc
  cloud_save/save_service.cc
  cloud_save/save_controller.cc
  inventory/inv_service.cc
  inventory/inv_controller.cc
  remote_config/config_service.cc
  remote_config/config_controller.cc
  analytics/analytics_service.cc
  analytics/analytics_controller.cc
  live_events/events_service.cc
  live_events/events_controller.cc
  gateway/admin_filters.cc
  admin/admin_service.cc
  admin/admin_controller.cc
  realtime/hub.cc
  realtime/ws_controller.cc
  replays/replay_service.cc
  replays/replay_controller.cc
  asset_registry/asset_service.cc
  asset_registry/asset_controller.cc
  test_runner/testrun_service.cc
  test_runner/testrun_controller.cc
  observability/metrics.cc
  app_setup.cc
)
target_include_directories(baas_core PUBLIC ${CMAKE_SOURCE_DIR})   # so "baas/..." headers resolve
target_link_libraries(baas_core PUBLIC Drogon::Drogon PkgConfig::SODIUM PRIVATE engine_flags)

# ---- the server executable ---------------------------------------------------
add_executable(baas main.cc)
target_link_libraries(baas PRIVATE baas_core engine_flags)

# ---- tests (need Drogon, so they live here, not in the root) ------------------
find_package(CURL REQUIRED)   # libcurl drives real HTTP in the integration tests

add_executable(test_baas_jwt ${CMAKE_SOURCE_DIR}/tests/test_baas_jwt.cc)
target_link_libraries(test_baas_jwt PRIVATE baas_core engine_flags)
add_test(NAME baas_jwt COMMAND test_baas_jwt)

# Rate limiter: pure token-bucket logic, deterministic (injected time) — no server.
add_executable(test_rate_limiter ${CMAKE_SOURCE_DIR}/tests/test_rate_limiter.cc)
target_link_libraries(test_rate_limiter PRIVATE baas_core engine_flags)
add_test(NAME rate_limiter COMMAND test_rate_limiter)

# Metrics: pure counter logic (record → snapshot, path normalization) — no server.
add_executable(test_metrics ${CMAKE_SOURCE_DIR}/tests/test_metrics.cc)
target_link_libraries(test_metrics PRIVATE baas_core engine_flags)
add_test(NAME metrics COMMAND test_metrics)

add_executable(test_baas_auth ${CMAKE_SOURCE_DIR}/tests/test_baas_auth.cc)
target_link_libraries(test_baas_auth PRIVATE baas_core CURL::libcurl engine_flags)
add_test(NAME baas_auth COMMAND test_baas_auth)

add_executable(test_baas_leaderboard ${CMAKE_SOURCE_DIR}/tests/test_baas_leaderboard.cc)
target_link_libraries(test_baas_leaderboard PRIVATE baas_core CURL::libcurl engine_flags)
add_test(NAME baas_leaderboard COMMAND test_baas_leaderboard)

add_executable(test_baas_cloudsave ${CMAKE_SOURCE_DIR}/tests/test_baas_cloudsave.cc)
target_link_libraries(test_baas_cloudsave PRIVATE baas_core CURL::libcurl engine_flags)
add_test(NAME baas_cloudsave COMMAND test_baas_cloudsave)

add_executable(test_baas_inventory ${CMAKE_SOURCE_DIR}/tests/test_baas_inventory.cc)
target_link_libraries(test_baas_inventory PRIVATE baas_core CURL::libcurl engine_flags)
add_test(NAME baas_inventory COMMAND test_baas_inventory)

add_executable(test_baas_assets ${CMAKE_SOURCE_DIR}/tests/test_baas_assets.cc)
target_link_libraries(test_baas_assets PRIVATE baas_core CURL::libcurl engine_flags)
add_test(NAME baas_assets COMMAND test_baas_assets)

add_executable(test_baas_testruns ${CMAKE_SOURCE_DIR}/tests/test_baas_testruns.cc)
target_link_libraries(test_baas_testruns PRIVATE baas_core CURL::libcurl engine_flags)
add_test(NAME baas_testruns COMMAND test_baas_testruns)

foreach(svc config analytics events admin replays ratelimit metrics)
  add_executable(test_baas_${svc} ${CMAKE_SOURCE_DIR}/tests/test_baas_${svc}.cc)
  target_link_libraries(test_baas_${svc} PRIVATE baas_core CURL::libcurl engine_flags)
  add_test(NAME baas_${svc} COMMAND test_baas_${svc})
endforeach()

# End-to-end: the real SDK (libcurl transport) against a live server. Links the
# SDK + the server + curl (for the healthz probe in the harness).
# test_sdk_live also drives the full test-runner loop, so it compiles the worker
# (process_one) + registry.cpp and links runner_core (run_scenario + sandbox sim).
add_executable(test_sdk_live ${CMAKE_SOURCE_DIR}/tests/test_sdk_live.cc
  ${CMAKE_SOURCE_DIR}/src/games/runner/worker.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/ecs/registry.cpp)
target_link_libraries(test_sdk_live PRIVATE gbaas_sdk baas_core runner_core CURL::libcurl engine_flags)
add_test(NAME sdk_live COMMAND test_sdk_live)

# End-to-end realtime: the SDK's NATIVE ws:// transport against a live server.
add_executable(test_sdk_realtime_live ${CMAKE_SOURCE_DIR}/tests/test_sdk_realtime_live.cc)
target_link_libraries(test_sdk_realtime_live PRIVATE gbaas_sdk baas_core CURL::libcurl engine_flags)
add_test(NAME sdk_realtime_live COMMAND test_sdk_realtime_live)
