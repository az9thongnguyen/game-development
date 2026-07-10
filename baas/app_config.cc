// =============================================================================
//  baas/app_config.cc  —  see app_config.h
// =============================================================================
#include "baas/app_config.h"

#include <utility>

namespace web {
namespace {
AppConfig g_config{"dev-insecure-secret-change-me", 3600};
}

void             set_config(AppConfig cfg) { g_config = std::move(cfg); }
const AppConfig& config() { return g_config; }

}  // namespace web
