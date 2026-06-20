// =============================================================================
//  server/net.hpp  —  the ONLY file that touches POSIX sockets
// =============================================================================
//  Isolating the OS networking here mirrors the engine's platform seam: all the
//  parsing/routing/leaderboard logic stays socket-free and unit-tested, and this
//  thin layer just moves bytes. Blocking, one connection at a time, Connection:
//  close — plenty for a local distribution/dev server.
// =============================================================================
#pragma once

#include <functional>
#include <string>

#include "server/http.hpp"

namespace web {

using Handler = std::function<Response(const Request&)>;

// Bind host:port and serve forever, calling `handler` per request. Returns nonzero
// on a fatal setup error (socket/bind/listen). `host` is an IPv4 literal
// ("127.0.0.1" to stay local, "0.0.0.0" to expose on the LAN).
int serve(const std::string& host, int port, const Handler& handler);

} // namespace web
