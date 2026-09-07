// =============================================================================
//  baas/cloud_save/save_service.cc  —  see save_service.h
// =============================================================================
#include "baas/cloud_save/save_service.h"

#include <cctype>

#include "baas/db/db.h"

namespace web::save {

bool valid_slot(const std::string& slot) {
    if (slot.empty() || slot.size() > 64) return false;
    for (char c : slot)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
            return false;
    return true;
}

PutResult put(long project_id, long user_id, const std::string& slot,
              const std::string& data, long long if_match) {
    // Read-then-write, so: a transaction, the row materialised, then a locking read
    // (chapter 141). This used to be a bare SELECT followed by a bare INSERT-or-UPDATE
    // with nothing around it — two devices saving the same slot for the first time both
    // saw nothing and both inserted, which on a real pool is an uncaught UniqueViolation
    // and a dead process. Version 0 is the materialised value, so a brand-new save still
    // lands at version 1 and an `if_match` against a row that did not exist still fails.
    db::Transaction tx(db::client());
    try {
        db::ensure_row(tx,
            "INSERT INTO saves(project_id, user_id, slot, data, version) VALUES(?,?,?,'',0)",
            project_id, user_id, slot);
        const auto cur = db::exec(tx,
            std::string("SELECT version FROM saves WHERE project_id=? AND user_id=? AND slot=?") +
                db::lock_clause(),
            project_id, user_id, slot);
        const long long have = cur.empty() ? 0 : cur[0]["version"].as<long>();

        if (if_match > 0 && have != if_match) {
            // ROLLBACK, not just return: the materialised row is this transaction's, and
            // a refused write must not leave an empty save behind.
            tx.rollback();
            return {std::nullopt, Error{409, "version_conflict", "save was modified"}};
        }

        const long long new_version = have + 1;
        db::exec(tx,
            "UPDATE saves SET data=?, version=?, updated_at=CURRENT_TIMESTAMP "
            "WHERE project_id=? AND user_id=? AND slot=?",
            data, new_version, project_id, user_id, slot);
        return {Meta{slot, new_version, static_cast<long long>(data.size()), ""}, std::nullopt};
    } catch (const std::exception&) {
        tx.rollback();
        return {std::nullopt, Error{500, "internal", "save failed"}};
    }
}

std::optional<Record> get(long project_id, long user_id, const std::string& slot) {
    const auto rows = db::exec(db::client(),
        "SELECT data, version, updated_at FROM saves "
        "WHERE project_id=? AND user_id=? AND slot=?",
        project_id, user_id, slot);
    if (rows.empty()) return std::nullopt;
    return Record{slot, rows[0]["version"].as<long>(), rows[0]["data"].as<std::string>(),
                  rows[0]["updated_at"].as<std::string>()};
}

std::vector<Meta> list(long project_id, long user_id) {
    const auto rows = db::exec(db::client(),
        "SELECT slot, version, BYTELEN(data) AS sz, updated_at FROM saves "
        "WHERE project_id=? AND user_id=? ORDER BY slot ASC",
        project_id, user_id);
    std::vector<Meta> out;
    for (const auto& r : rows)
        out.push_back({r["slot"].as<std::string>(), r["version"].as<long>(),
                       r["sz"].as<long>(), r["updated_at"].as<std::string>()});
    return out;
}

bool remove(long project_id, long user_id, const std::string& slot) {
    const auto r = db::exec(db::client(),
        "DELETE FROM saves WHERE project_id=? AND user_id=? AND slot=?",
        project_id, user_id, slot);
    return r.affectedRows() > 0;
}

}  // namespace web::save
