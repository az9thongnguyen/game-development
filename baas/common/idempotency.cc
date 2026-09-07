// =============================================================================
//  baas/common/idempotency.cc  —  see idempotency.h
// =============================================================================
#include "baas/common/idempotency.h"

#include "baas/db/db.h"

namespace web::idem {

std::optional<long long> lookup(long project_id, const std::string& key) {
    const auto rows = db::exec(db::client(),
        "SELECT result FROM idempotency_keys WHERE project_id=? AND idem_key=?",
        project_id, key);
    if (rows.empty()) return std::nullopt;
    return rows[0]["result"].as<long long>();
}

void record_with(const std::shared_ptr<drogon::orm::Transaction>& db, long project_id,
                 const std::string& key, long long result) {
    // ON CONFLICT DO NOTHING (portable across SQLite >= 3.24 and Postgres): a racing
    // duplicate is a harmless no-op, first writer wins.
    db::exec(db,
        "INSERT INTO idempotency_keys(project_id, idem_key, result) VALUES(?,?,?) "
        "ON CONFLICT(project_id, idem_key) DO NOTHING",
        project_id, key, result);
}

}  // namespace web::idem
