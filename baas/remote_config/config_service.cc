// =============================================================================
//  baas/remote_config/config_service.cc  —  see config_service.h
// =============================================================================
#include "baas/remote_config/config_service.h"

#include "baas/admin/audit.h"
#include "baas/db/db.h"

namespace web::cfg {

std::vector<KV> all(long project_id) {
    const auto rows = db::exec(db::client(),
        "SELECT key, value FROM config WHERE project_id=? ORDER BY key ASC", project_id);
    std::vector<KV> out;
    for (const auto& r : rows)
        out.push_back({r["key"].as<std::string>(), r["value"].as<std::string>()});
    return out;
}

std::optional<std::string> get(long project_id, const std::string& key) {
    const auto rows = db::exec(db::client(),
        "SELECT value FROM config WHERE project_id=? AND key=?", project_id, key);
    if (rows.empty()) return std::nullopt;
    return rows[0]["value"].as<std::string>();
}

void set(long project_id, const std::string& key, const std::string& value) {
    // ONE statement, because this one never needs the old value. Until chapter 141 it
    // was a read and then an INSERT or an UPDATE, with no transaction around either —
    // so two operators setting the same new key at the same moment both found nothing
    // and both inserted, and on a backend with a real pool the loser is an uncaught
    // UniqueViolation, which is to say a dead server process. An upsert cannot lose
    // that race because there is nothing between the read and the write.
    db::exec(db::client(),
        "INSERT INTO config(project_id, key, value) VALUES(?,?,?) "
        "ON CONFLICT(project_id, key) "
        "DO UPDATE SET value=excluded.value, updated_at=CURRENT_TIMESTAMP",
        project_id, key, value);
}

bool remove(long project_id, const std::string& key) {
    const auto r = db::exec(db::client(),
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
