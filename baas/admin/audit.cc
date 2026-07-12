// =============================================================================
//  baas/admin/audit.cc  —  see audit.h
// =============================================================================
#include "baas/admin/audit.h"

#include <trantor/utils/Logger.h>

#include "baas/db/db.h"

namespace web::audit {

void record(long project_id, const std::string& actor, const std::string& action,
            const std::string& detail) {
    const auto db = db::client();
    if (!db) return;   // no client (e.g. a unit test that never set one) — nothing to do
    try {
        if (project_id == 0) {
            // Platform-level action: store NULL so it is distinct from project id 0.
            db->execSqlSync(
                "INSERT INTO audit_log(project_id, actor, action, detail) "
                "VALUES(NULL, ?, ?, ?)",
                actor, action, detail);
        } else {
            db->execSqlSync(
                "INSERT INTO audit_log(project_id, actor, action, detail) "
                "VALUES(?, ?, ?, ?)",
                project_id, actor, action, detail);
        }
    } catch (const std::exception& e) {
        // Auditing must never break the action it decorates. Log and move on.
        LOG_ERROR << "audit.record failed: " << e.what();
    }
}

std::vector<Entry> recent(long project_id, int limit) {
    const auto db = db::client();
    std::vector<Entry> out;
    if (!db) return out;
    const auto rows =
        project_id == 0
            ? db->execSqlSync(
                  "SELECT id, project_id, actor, action, detail, created_at "
                  "FROM audit_log WHERE project_id IS NULL ORDER BY id DESC LIMIT ?",
                  limit)
            : db->execSqlSync(
                  "SELECT id, project_id, actor, action, detail, created_at "
                  "FROM audit_log WHERE project_id=? ORDER BY id DESC LIMIT ?",
                  project_id, limit);
    for (const auto& r : rows)
        out.push_back({r["id"].as<long>(),
                       r["project_id"].isNull() ? 0 : r["project_id"].as<long>(),
                       r["actor"].as<std::string>(), r["action"].as<std::string>(),
                       r["detail"].as<std::string>(), r["created_at"].as<std::string>()});
    return out;
}

}  // namespace web::audit
