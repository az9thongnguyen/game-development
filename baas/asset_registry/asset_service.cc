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
    // Materialise, lock, then write — the same shape as cloud save, and for the same
    // reason: two editors publishing the same NEW asset both read nothing and both
    // inserted (chapter 141).
    auto tx = db::client()->newTransaction();
    try {
        db::ensure_row(tx,
            "INSERT INTO assets(project_id, name, kind, data, version) VALUES(?,?,'','',0)",
            project_id, name);
        const auto cur = db::exec(tx,
            std::string("SELECT version FROM assets WHERE project_id=? AND name=?") +
                db::lock_clause(),
            project_id, name);
        const long long have = cur.empty() ? 0 : cur[0]["version"].as<long>();

        if (if_match > 0 && have != if_match) {
            tx->rollback();   // do not leave the materialised empty asset behind
            return {std::nullopt, Error{409, "version_conflict", "asset was modified"}};
        }

        const long long new_version = have + 1;
        db::exec(tx,
            "UPDATE assets SET kind=?, data=?, version=?, updated_at=CURRENT_TIMESTAMP "
            "WHERE project_id=? AND name=?",
            kind, data, new_version, project_id, name);
        return {Meta{name, kind, new_version, static_cast<long long>(data.size()), ""},
                std::nullopt};
    } catch (const std::exception&) {
        tx->rollback();
        return {std::nullopt, Error{500, "internal", "asset put failed"}};
    }
}

std::optional<Record> get(long project_id, const std::string& name) {
    const auto rows = db::exec(db::client(),
        "SELECT kind, data, version, updated_at FROM assets WHERE project_id=? AND name=?",
        project_id, name);
    if (rows.empty()) return std::nullopt;
    return Record{name, rows[0]["kind"].as<std::string>(), rows[0]["version"].as<long>(),
                  rows[0]["data"].as<std::string>(), rows[0]["updated_at"].as<std::string>()};
}

std::vector<Meta> list(long project_id, const std::string& kind_filter) {
    // One query with an optional kind filter; BYTELEN(data) = payload bytes.
    const char* sql_all =
        "SELECT name, kind, version, BYTELEN(data) AS sz, updated_at FROM assets "
        "WHERE project_id=? ORDER BY name ASC";
    const char* sql_kind =
        "SELECT name, kind, version, BYTELEN(data) AS sz, updated_at FROM assets "
        "WHERE project_id=? AND kind=? ORDER BY name ASC";
    const auto rows = kind_filter.empty()
        ? db::exec(db::client(), sql_all, project_id)
        : db::exec(db::client(), sql_kind, project_id, kind_filter);
    std::vector<Meta> out;
    for (const auto& r : rows)
        out.push_back({r["name"].as<std::string>(), r["kind"].as<std::string>(),
                       r["version"].as<long>(), r["sz"].as<long>(),
                       r["updated_at"].as<std::string>()});
    return out;
}

bool remove(long project_id, const std::string& name) {
    const auto r = db::exec(db::client(),
        "DELETE FROM assets WHERE project_id=? AND name=?", project_id, name);
    return r.affectedRows() > 0;
}

}  // namespace web::asset
