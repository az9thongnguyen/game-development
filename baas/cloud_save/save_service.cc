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
    auto       db       = db::client();
    const auto existing = db->execSqlSync(
        "SELECT version FROM saves WHERE project_id=? AND user_id=? AND slot=?",
        project_id, user_id, slot);

    if (if_match > 0) {   // caller requires the current version to match
        if (existing.empty() || existing[0]["version"].as<long>() != if_match)
            return {std::nullopt, Error{409, "version_conflict", "save was modified"}};
    }

    long long new_version = 1;
    if (existing.empty()) {
        db->execSqlSync(
            "INSERT INTO saves(project_id, user_id, slot, data, version) VALUES(?,?,?,?,1)",
            project_id, user_id, slot, data);
    } else {
        new_version = existing[0]["version"].as<long>() + 1;
        db->execSqlSync(
            "UPDATE saves SET data=?, version=?, updated_at=CURRENT_TIMESTAMP "
            "WHERE project_id=? AND user_id=? AND slot=?",
            data, new_version, project_id, user_id, slot);
    }
    return {Meta{slot, new_version, static_cast<long long>(data.size()), ""}, std::nullopt};
}

std::optional<Record> get(long project_id, long user_id, const std::string& slot) {
    const auto rows = db::client()->execSqlSync(
        "SELECT data, version, updated_at FROM saves "
        "WHERE project_id=? AND user_id=? AND slot=?",
        project_id, user_id, slot);
    if (rows.empty()) return std::nullopt;
    return Record{slot, rows[0]["version"].as<long>(), rows[0]["data"].as<std::string>(),
                  rows[0]["updated_at"].as<std::string>()};
}

std::vector<Meta> list(long project_id, long user_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT slot, version, length(CAST(data AS BLOB)) AS sz, updated_at FROM saves "
        "WHERE project_id=? AND user_id=? ORDER BY slot ASC",
        project_id, user_id);
    std::vector<Meta> out;
    for (const auto& r : rows)
        out.push_back({r["slot"].as<std::string>(), r["version"].as<long>(),
                       r["sz"].as<long>(), r["updated_at"].as<std::string>()});
    return out;
}

bool remove(long project_id, long user_id, const std::string& slot) {
    const auto r = db::client()->execSqlSync(
        "DELETE FROM saves WHERE project_id=? AND user_id=? AND slot=?",
        project_id, user_id, slot);
    return r.affectedRows() > 0;
}

}  // namespace web::save
