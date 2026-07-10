// =============================================================================
//  baas/remote_config/config_service.h  —  project-scoped remote config (read)
// =============================================================================
//  Server-controlled key/value settings a game reads at startup (feature flags,
//  tunables, message-of-the-day). Client-facing L1 is read-only; writing config
//  is an admin action that belongs to the dashboard (L3). Namespace `web::cfg`
//  avoids clashing with the `web::config()` app-config accessor.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace web::cfg {

struct KV {
    std::string key;
    std::string value;
};

std::vector<KV>            all(long project_id);
std::optional<std::string> get(long project_id, const std::string& key);

// admin (dashboard) writes
void set(long project_id, const std::string& key, const std::string& value);   // upsert
bool remove(long project_id, const std::string& key);                          // true if deleted

}  // namespace web::cfg
