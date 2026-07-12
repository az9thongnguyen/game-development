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

// Audited LiveOps change: upsert `key`, record the old→new transition in the audit
// log, and return the previous value (nullopt if the key was unset). The return value
// is what an operator sets back to revert — this is what makes a LiveOps change
// *reversible* (H2 exit gate) and auditable. Clients keep reading via /v1/config, so
// the change takes effect with no client redeployment.
std::optional<std::string> set_audited(long project_id, const std::string& key,
                                       const std::string& value, const std::string& actor);

// Audited delete: record the removed key's old value (so it is revertible via set)
// and return it; nullopt if there was no such key.
std::optional<std::string> remove_audited(long project_id, const std::string& key,
                                          const std::string& actor);

}  // namespace web::cfg
