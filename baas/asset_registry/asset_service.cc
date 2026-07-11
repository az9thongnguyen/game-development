// =============================================================================
//  baas/asset_registry/asset_service.cc  —  see asset_service.h
// =============================================================================
#include "baas/asset_registry/asset_service.h"

#include <cctype>

#include "baas/db/db.h"

namespace web::asset {

bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 128) return false;
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
            return false;
    return true;
}

bool valid_kind(const std::string& kind) {
    if (kind.size() > 32) return false;
    for (char c : kind)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
            return false;
    return true;
}

PutResult put(long project_id, const std::string& name, const std::string& kind,
              const std::string& data, long long if_match) {
    auto       db       = db::client();
    const auto existing = db->execSqlSync(
        "SELECT version FROM assets WHERE project_id=? AND name=?", project_id, name);

    if (if_match > 0) {   // caller requires the current version to match
        if (existing.empty() || existing[0]["version"].as<long>() != if_match)
            return {std::nullopt, Error{409, "version_conflict", "asset was modified"}};
    }

    long long new_version = 1;
    if (existing.empty()) {
        db->execSqlSync(
            "INSERT INTO assets(project_id, name, kind, data, version) VALUES(?,?,?,?,1)",
            project_id, name, kind, data);
    } else {
        new_version = existing[0]["version"].as<long>() + 1;
        db->execSqlSync(
            "UPDATE assets SET kind=?, data=?, version=?, updated_at=CURRENT_TIMESTAMP "
            "WHERE project_id=? AND name=?",
            kind, data, new_version, project_id, name);
    }
    return {Meta{name, kind, new_version, static_cast<long long>(data.size()), ""}, std::nullopt};
}

std::optional<Record> get(long project_id, const std::string& name) {
    const auto rows = db::client()->execSqlSync(
        "SELECT kind, data, version, updated_at FROM assets WHERE project_id=? AND name=?",
        project_id, name);
    if (rows.empty()) return std::nullopt;
    return Record{name, rows[0]["kind"].as<std::string>(), rows[0]["version"].as<long>(),
                  rows[0]["data"].as<std::string>(), rows[0]["updated_at"].as<std::string>()};
}

std::vector<Meta> list(long project_id, const std::string& kind_filter) {
    // One query with an optional kind filter; length(CAST(data AS BLOB)) = payload bytes.
    const char* sql_all =
        "SELECT name, kind, version, length(CAST(data AS BLOB)) AS sz, updated_at FROM assets "
        "WHERE project_id=? ORDER BY name ASC";
    const char* sql_kind =
        "SELECT name, kind, version, length(CAST(data AS BLOB)) AS sz, updated_at FROM assets "
        "WHERE project_id=? AND kind=? ORDER BY name ASC";
    const auto rows = kind_filter.empty()
        ? db::client()->execSqlSync(sql_all, project_id)
        : db::client()->execSqlSync(sql_kind, project_id, kind_filter);
    std::vector<Meta> out;
    for (const auto& r : rows)
        out.push_back({r["name"].as<std::string>(), r["kind"].as<std::string>(),
                       r["version"].as<long>(), r["sz"].as<long>(),
                       r["updated_at"].as<std::string>()});
    return out;
}

bool remove(long project_id, const std::string& name) {
    const auto r = db::client()->execSqlSync(
        "DELETE FROM assets WHERE project_id=? AND name=?", project_id, name);
    return r.affectedRows() > 0;
}

}  // namespace web::asset
