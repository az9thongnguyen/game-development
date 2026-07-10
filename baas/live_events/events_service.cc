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

}  // namespace web::live
