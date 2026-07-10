// =============================================================================
//  baas/app_setup.h  —  register the framework-level (lambda) routes
// =============================================================================
//  Controllers (auth, leaderboard) auto-register when their .cc is linked, but
//  the plain lambda routes (/healthz, /v1/ping) are registered here so that BOTH
//  the server (main.cc) and the integration tests set up an identical app. Call
//  once before app().run().
// =============================================================================
#pragma once

namespace web {

void register_routes();

}  // namespace web
