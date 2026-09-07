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
  common/idempotency.cc
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
  store/store_service.cc
  store/store_controller.cc
  rbac/rbac.cc
  remote_config/config_service.cc
  remote_config/config_controller.cc
  analytics/analytics_service.cc
  analytics/analytics_controller.cc
  live_events/events_service.cc
  live_events/events_controller.cc
  gateway/admin_filters.cc
  admin/admin_service.cc
  admin/admin_controller.cc
  admin/audit.cc
  realtime/hub.cc
  realtime/ws_controller.cc
  replays/replay_service.cc
  replays/replay_controller.cc
  asset_registry/asset_service.cc
  asset_registry/asset_controller.cc
  test_runner/testrun_service.cc
  test_runner/testrun_controller.cc
  observability/metrics.cc
  openapi/openapi.cc
  openapi/spec.cc
  app_setup.cc
)
# ${CMAKE_SOURCE_DIR} so "baas/..." headers resolve; ${CMAKE_SOURCE_DIR}/src so the
# ONE header the backend shares with the engine does — engine/elo.hpp, header-only,
# constexpr, no library. That is a deliberate exception to "links no engine code" and
# it is the narrowest possible one: the whole point of an integer rating curve is that
# the client and the server compute the SAME number, and a copy of the table in each
# is precisely the bug it exists to prevent. Nothing is linked; see chapter 139.
target_include_directories(baas_core PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
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

# Migrations: the versioned schema engine + audit log — pure DB, no HTTP server.
add_executable(test_baas_migrations ${CMAKE_SOURCE_DIR}/tests/test_baas_migrations.cc)
target_link_libraries(test_baas_migrations PRIVATE baas_core engine_flags)
add_test(NAME baas_migrations COMMAND test_baas_migrations)

# LiveOps: reversible, audited remote-config change (H2 exit gate) — pure DB.
add_executable(test_baas_liveops ${CMAKE_SOURCE_DIR}/tests/test_baas_liveops.cc)
target_link_libraries(test_baas_liveops PRIVATE baas_core engine_flags)
add_test(NAME baas_liveops COMMAND test_baas_liveops)

# Release metrics: per-release analytics attribution (H2 exit gate) — pure DB.
add_executable(test_baas_release_metrics ${CMAKE_SOURCE_DIR}/tests/test_baas_release_metrics.cc)
target_link_libraries(test_baas_release_metrics PRIVATE baas_core engine_flags)
add_test(NAME baas_release_metrics COMMAND test_baas_release_metrics)

# Secret rotation: mint-new + invalidate-old + audit (H2 RBAC) — pure DB.
add_executable(test_baas_secret_rotation ${CMAKE_SOURCE_DIR}/tests/test_baas_secret_rotation.cc)
target_link_libraries(test_baas_secret_rotation PRIVATE baas_core engine_flags)
add_test(NAME baas_secret_rotation COMMAND test_baas_secret_rotation)

# The dialect seam: the one line of SQL that differs between the two backends.
add_executable(test_baas_dialect ${CMAKE_SOURCE_DIR}/tests/test_baas_dialect.cc)
target_link_libraries(test_baas_dialect PRIVATE baas_core CURL::libcurl engine_flags)
add_test(NAME baas_dialect COMMAND test_baas_dialect)

# Concurrency: eight buyers, one purse. Toothless on SQLite by construction (see the
# file), which is exactly why the Postgres run exists.
# The API description, and the two things that keep it from drifting: it is compared to
# the routes Drogon actually registered, and its generated bytes are compared to the
# committed baas/openapi.json. REPO_ROOT because half of this test reads a file from the
# source tree rather than from the asset root.
add_executable(test_baas_openapi ${CMAKE_SOURCE_DIR}/tests/test_baas_openapi.cc)
target_link_libraries(test_baas_openapi PRIVATE baas_core CURL::libcurl engine_flags)
target_compile_definitions(test_baas_openapi PRIVATE REPO_ROOT="${CMAKE_SOURCE_DIR}")
add_test(NAME baas_openapi COMMAND test_baas_openapi)

# The SQL seam: the translation from this codebase's one dialect into the backend's,
# and the source-level proof that nothing goes around it. BAAS_SRC_DIR because half of
# this test reads the tree it is testing.
add_executable(test_baas_sql_seam ${CMAKE_SOURCE_DIR}/tests/test_baas_sql_seam.cc)
target_link_libraries(test_baas_sql_seam PRIVATE baas_core CURL::libcurl engine_flags)
target_compile_definitions(test_baas_sql_seam PRIVATE
    BAAS_SRC_DIR="${CMAKE_SOURCE_DIR}/baas" TESTS_SRC_DIR="${CMAKE_SOURCE_DIR}/tests")
add_test(NAME baas_sql_seam COMMAND test_baas_sql_seam)

add_executable(test_baas_concurrency ${CMAKE_SOURCE_DIR}/tests/test_baas_concurrency.cc)
target_link_libraries(test_baas_concurrency PRIVATE baas_core CURL::libcurl engine_flags)
add_test(NAME baas_concurrency COMMAND test_baas_concurrency)

# Idempotency: retry-safe inventory grant (H2 correctness) — pure DB.
add_executable(test_baas_idempotency ${CMAKE_SOURCE_DIR}/tests/test_baas_idempotency.cc)
target_link_libraries(test_baas_idempotency PRIVATE baas_core engine_flags)
add_test(NAME baas_idempotency COMMAND test_baas_idempotency)

# Purchase: atomic spend+grant with rollback + idempotency (H2 economy) — pure DB.
add_executable(test_baas_purchase ${CMAKE_SOURCE_DIR}/tests/test_baas_purchase.cc)
target_link_libraries(test_baas_purchase PRIVATE baas_core engine_flags)
add_test(NAME baas_purchase COMMAND test_baas_purchase)

# Catalog: priced offers + buy-by-SKU over the atomic purchase (H2 economy) — pure DB.
add_executable(test_baas_catalog ${CMAKE_SOURCE_DIR}/tests/test_baas_catalog.cc)
target_link_libraries(test_baas_catalog PRIVATE baas_core engine_flags)
add_test(NAME baas_catalog COMMAND test_baas_catalog)

# RBAC: operators + roles + authenticate + authorize (H2 RBAC) — pure DB.
add_executable(test_baas_rbac ${CMAKE_SOURCE_DIR}/tests/test_baas_rbac.cc)
target_link_libraries(test_baas_rbac PRIVATE baas_core engine_flags)
add_test(NAME baas_rbac COMMAND test_baas_rbac)

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

# Tracing: X-Request-Id correlation on every response (H2 telemetry) — boots a server.
add_executable(test_baas_tracing ${CMAKE_SOURCE_DIR}/tests/test_baas_tracing.cc)
target_link_libraries(test_baas_tracing PRIVATE baas_core CURL::libcurl engine_flags)
add_test(NAME baas_tracing COMMAND test_baas_tracing)

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

# End-to-end: the FARM against a live server. Not "the SDK works" (test_sdk_live has
# that) but "an operator changes a price and a running game charges it" — the chain
# from the admin route through the seed, the SDK, the override parser and the day's
# sale. Compiles the scene + its engine deps because the thing under test is the GAME.
add_executable(test_farm_live ${CMAKE_SOURCE_DIR}/tests/test_farm_live.cc
  ${CMAKE_SOURCE_DIR}/src/games/farm/farm_scene.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/renderer2d.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/image.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/assets.cpp)
target_include_directories(test_farm_live PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_farm_live PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_farm_live PRIVATE gbaas_sdk baas_core farm_core ui_core text_core
  CURL::libcurl engine_flags)
add_test(NAME farm_live COMMAND test_farm_live)

# End-to-end realtime: the SDK's NATIVE ws:// transport against a live server.
#
# Gated on exactly what the TRANSPORT is gated on. sdk/cpp/CMakeLists.txt looks for
# curl/websockets.h (libcurl >= 7.86) and, when it is missing, compiles the native
# realtime transport as an inert stub and says so. This test drives raw ws frames with
# curl_ws_recv, so without that header it does not COMPILE — and it was added
# unconditionally, which meant the SDK degraded gracefully and the test that exercises
# the degradation took the whole `baas` half of the build down with it.
#
# Nobody saw it because the only machine that ever built this half was a Mac with
# Homebrew's curl 8.x. Ubuntu 22.04 ships 7.81 — and Ubuntu 22.04 is the base of the
# backend's own container image, so "any machine" included CI the moment CI started
# building it (chapter 129).
#
# The message is not decoration: a test that vanishes quietly is the exact failure this
# whole slice exists to end, so a skip has to be as loud as a failure.
if(GBAAS_CURL_WS_INC AND GBAAS_CURL_WS_LIB)
  add_executable(test_sdk_realtime_live ${CMAKE_SOURCE_DIR}/tests/test_sdk_realtime_live.cc)
  target_link_libraries(test_sdk_realtime_live PRIVATE gbaas_sdk baas_core CURL::libcurl engine_flags)
  add_test(NAME sdk_realtime_live COMMAND test_sdk_realtime_live)
  # Two players, one battle, a real socket (chapter 139). Same gate, same reason:
  # without a ws-capable curl there is no transport for a match to happen over.
  add_executable(test_creature_pvp_live ${CMAKE_SOURCE_DIR}/tests/test_creature_pvp_live.cc
                 ${CMAKE_SOURCE_DIR}/src/engine/assets.cpp)
  target_include_directories(test_creature_pvp_live PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_compile_definitions(test_creature_pvp_live PRIVATE ASSET_ROOT="${CMAKE_SOURCE_DIR}")
  target_link_libraries(test_creature_pvp_live
                        PRIVATE gbaas_sdk baas_core creature_pvp CURL::libcurl engine_flags)
  add_test(NAME creature_pvp_live COMMAND test_creature_pvp_live)
else()
  message(STATUS
    "baas: SKIPPING sdk_realtime_live and creature_pvp_live — this libcurl has no "
    "WebSocket API (needs >= 7.86). The SDK's native realtime is a stub in this build "
    "too, so the tests would have nothing to drive.")
endif()

# A TIMEOUT on every test in this directory. Not a tidiness setting: chapter 140
# added transactions to five read-then-write paths, one of them self-deadlocked on
# SQLite (a transaction holds the only connection in the pool, and the code that ran
# next asked the pool for one), and the suite simply STOPPED. ctest's default timeout
# is 1500 s, so a deadlock costs twenty-five minutes before anything says "failed" —
# and a hang and a slow test look identical until then.
get_property(_baas_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_baas_tests} PROPERTIES TIMEOUT 120)

# ---- one target for the Drogon-gated half ------------------------------------
# CI runs the engine's 48 tests in a job with no Drogon, and these 28 in a job that
# has it. That second job must not rebuild the first 48 to reach them, and it must
# not carry a hand-written list of target names either — a list is a thing that goes
# stale the day somebody adds a test and forgets it, which is exactly how this half
# stayed dark in the first place.
#
# BUILDSYSTEM_TARGETS is every target THIS directory defines, so the grouping is the
# directory, the same thing `ctest --test-dir build/baas` already selects on.
get_property(baas_dir_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
add_custom_target(baas_tests)
add_dependencies(baas_tests ${baas_dir_targets})
