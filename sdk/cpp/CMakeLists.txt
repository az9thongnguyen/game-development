# =============================================================================
#  sdk/cpp/CMakeLists.txt  —  gbaas_sdk, the unified C++ SDK
# =============================================================================
#  Game-facing: linked by a game (native and WASM). It must NOT depend on Drogon
#  or any server code. Its only native dependency is libcurl; the web build uses
#  emscripten_fetch (no dependency). The transport is selected by platform.
# =============================================================================
add_library(gbaas_sdk STATIC
  src/client.cc
  src/realtime.cc
)
target_include_directories(gbaas_sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(EMSCRIPTEN)
  target_sources(gbaas_sdk PRIVATE src/transport_emscripten.cc src/ws_transport_emscripten.cc)
  target_link_options(gbaas_sdk PUBLIC -sFETCH)          # emscripten_fetch (async HTTP)
  target_link_options(gbaas_sdk PUBLIC -lwebsocket.js)   # emscripten_websocket (realtime)
else()
  target_sources(gbaas_sdk PRIVATE src/transport_curl.cc src/ws_transport_curl.cc)
  # Realtime needs a WebSocket-capable libcurl (>= 7.86, ships curl/websockets.h).
  # macOS's system libcurl lacks it, so prefer Homebrew's (keg-only) curl when it
  # has the header; otherwise fall back to find_package(CURL) and the native
  # realtime transport compiles as an inert stub (REST still works).
  find_path(GBAAS_CURL_WS_INC curl/websockets.h
            HINTS /opt/homebrew/opt/curl/include /usr/local/opt/curl/include)
  find_library(GBAAS_CURL_WS_LIB NAMES curl
            HINTS /opt/homebrew/opt/curl/lib /usr/local/opt/curl/lib)
  if(GBAAS_CURL_WS_INC AND GBAAS_CURL_WS_LIB)
    message(STATUS "gbaas_sdk: WebSocket-capable libcurl → ${GBAAS_CURL_WS_LIB}")
    target_include_directories(gbaas_sdk PRIVATE ${GBAAS_CURL_WS_INC})
    target_link_libraries(gbaas_sdk PUBLIC ${GBAAS_CURL_WS_LIB})
    target_compile_definitions(gbaas_sdk PRIVATE GBAAS_HAS_WS_CURL=1)
  else()
    message(STATUS "gbaas_sdk: no WebSocket-capable libcurl; native realtime is a stub")
    find_package(CURL REQUIRED)
    target_link_libraries(gbaas_sdk PUBLIC CURL::libcurl)
  endif()
endif()

target_link_libraries(gbaas_sdk PRIVATE engine_flags)
