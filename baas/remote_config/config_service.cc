// =============================================================================
//  baas/remote_config/config_service.cc  —  see config_service.h
// =============================================================================
#include "baas/remote_config/config_service.h"

#include "baas/db/db.h"

namespace web::cfg {

std::vector<KV> all(long project_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT key, value FROM config WHERE project_id=? ORDER BY key ASC", project_id);
    std::vector<KV> out;
    for (const auto& r : rows)
        out.push_back({r["key"].as<std::string>(), r["value"].as<std::string>()});
    return out;
}

std::optional<std::string> get(long project_id, const std::string& key) {
    const auto rows = db::client()->execSqlSync(
        "SELECT value FROM config WHERE project_id=? AND key=?", project_id, key);
    if (rows.empty()) return std::nullopt;
    return rows[0]["value"].as<std::string>();
}

}  // namespace web::cfg
