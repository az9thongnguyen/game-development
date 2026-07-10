// =============================================================================
//  baas/live_events/events_service.cc  —  see events_service.h
// =============================================================================
#include "baas/live_events/events_service.h"

#include "baas/db/db.h"

namespace web::live {

std::vector<Event> active(long project_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT key, name, payload FROM live_events WHERE project_id=? "
        "AND starts_at <= CURRENT_TIMESTAMP AND ends_at >= CURRENT_TIMESTAMP "
        "ORDER BY starts_at ASC",
        project_id);
    std::vector<Event> out;
    for (const auto& r : rows)
        out.push_back({r["key"].as<std::string>(), r["name"].as<std::string>(),
                       r["payload"].as<std::string>()});
    return out;
}

void create(long project_id, const std::string& key, const std::string& name,
            const std::string& starts_at, const std::string& ends_at, const std::string& payload) {
    auto       db = db::client();
    const auto ex = db->execSqlSync(
        "SELECT id FROM live_events WHERE project_id=? AND key=?", project_id, key);
    if (ex.empty())
        db->execSqlSync(
            "INSERT INTO live_events(project_id, key, name, starts_at, ends_at, payload) "
            "VALUES(?,?,?,?,?,?)",
            project_id, key, name, starts_at, ends_at, payload);
    else
        db->execSqlSync(
            "UPDATE live_events SET name=?, starts_at=?, ends_at=?, payload=? "
            "WHERE project_id=? AND key=?",
            name, starts_at, ends_at, payload, project_id, key);
}

}  // namespace web::live
