// =============================================================================
//  baas/replays/replay_service.cc  —  see replay_service.h
// =============================================================================
#include "baas/replays/replay_service.h"

#include "baas/db/db.h"

namespace web::replay {

bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (unsigned char c : name)
        if (c < 0x20 || c > 0x7E) return false;   // printable ASCII only
    return true;
}

long long create(long project_id, long user_id, const std::string& name, const std::string& data) {
    const auto r = db::client()->execSqlSync(
        "INSERT INTO replays(project_id, user_id, name, data) VALUES(?,?,?,?)",
        project_id, user_id, name, data);
    return static_cast<long long>(r.insertId());
}

std::optional<Record> get(long project_id, long user_id, long long id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT id, name, data, created_at FROM replays "
        "WHERE id=? AND project_id=? AND user_id=?",
        static_cast<long>(id), project_id, user_id);
    if (rows.empty()) return std::nullopt;
    return Record{rows[0]["id"].as<long>(), rows[0]["name"].as<std::string>(),
                  rows[0]["data"].as<std::string>(), rows[0]["created_at"].as<std::string>()};
}

std::vector<Meta> list(long project_id, long user_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT id, name, length(CAST(data AS BLOB)) AS sz, created_at FROM replays "
        "WHERE project_id=? AND user_id=? ORDER BY id DESC",
        project_id, user_id);
    std::vector<Meta> out;
    for (const auto& r : rows)
        out.push_back({r["id"].as<long>(), r["name"].as<std::string>(),
                       r["sz"].as<long>(), r["created_at"].as<std::string>()});
    return out;
}

bool remove(long project_id, long user_id, long long id) {
    const auto r = db::client()->execSqlSync(
        "DELETE FROM replays WHERE id=? AND project_id=? AND user_id=?",
        static_cast<long>(id), project_id, user_id);
    return r.affectedRows() > 0;
}

}  // namespace web::replay
