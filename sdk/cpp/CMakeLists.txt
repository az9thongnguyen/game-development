# =============================================================================
#  sdk/cpp/CMakeLists.txt  —  gbaas_sdk, the unified C++ SDK
# =============================================================================
#  Game-facing: linked by a game (native and WASM). It must NOT depend on Drogon
#  or any server code. Its only native dependency is libcurl; the web build uses
#  emscripten_fetch (no dependency). The transport is selected by platform.
# =============================================================================
add_library(gbaas_sdk STATIC
  src/client.cc
)
target_include_directories(gbaas_sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(EMSCRIPTEN)
  target_sources(gbaas_sdk PRIVATE src/transport_emscripten.cc)
  target_link_options(gbaas_sdk PUBLIC -sFETCH)          # emscripten_fetch (async HTTP)
else()
  find_package(CURL REQUIRED)
  target_sources(gbaas_sdk PRIVATE src/transport_curl.cc)
  target_link_libraries(gbaas_sdk PUBLIC CURL::libcurl)
endif()

target_link_libraries(gbaas_sdk PRIVATE engine_flags)
