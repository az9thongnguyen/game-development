// =============================================================================
//  baas/remote_config/config_service.cc  —  see config_service.h
// =============================================================================
#include "baas/remote_config/config_service.h"

#include "baas/admin/audit.h"
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

void set(long project_id, const std::string& key, const std::string& value) {
    auto       db = db::client();
    const auto ex = db->execSqlSync(
        "SELECT id FROM config WHERE project_id=? AND key=?", project_id, key);
    if (ex.empty())
        db->execSqlSync("INSERT INTO config(project_id, key, value) VALUES(?,?,?)",
                        project_id, key, value);
    else
        db->execSqlSync(
            "UPDATE config SET value=?, updated_at=CURRENT_TIMESTAMP WHERE project_id=? AND key=?",
            value, project_id, key);
}

bool remove(long project_id, const std::string& key) {
    const auto r = db::client()->execSqlSync(
        "DELETE FROM config WHERE project_id=? AND key=?", project_id, key);
    return r.affectedRows() > 0;
}

std::optional<std::string> set_audited(long project_id, const std::string& key,
                                       const std::string& value, const std::string& actor) {
    const auto old = get(project_id, key);
    set(project_id, key, value);
    audit::record(project_id, actor, "config.set",
                  "key=" + key + " old=" + old.value_or("<unset>") + " new=" + value);
    return old;
}

std::optional<std::string> remove_audited(long project_id, const std::string& key,
                                          const std::string& actor) {
    const auto old = get(project_id, key);
    if (!old) return std::nullopt;   // nothing to delete — no audit noise
    remove(project_id, key);
    audit::record(project_id, actor, "config.delete", "key=" + key + " old=" + *old);
    return old;
}

}  // namespace web::cfg
