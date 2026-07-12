// =============================================================================
//  baas/analytics/analytics_service.cc  —  see analytics_service.h
// =============================================================================
#include "baas/analytics/analytics_service.h"

#include <cctype>

#include "baas/db/db.h"

namespace web::analytics {

bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
            return false;
    return true;
}

void record(long project_id, long user_id, const std::string& name, const std::string& props,
            const std::string& release) {
    auto db = db::client();
    if (user_id > 0) {
        db->execSqlSync(
            "INSERT INTO analytics_events(project_id, user_id, name, props, release) "
            "VALUES(?,?,?,?,?)",
            project_id, user_id, name, props, release);
    } else {
        db->execSqlSync(
            "INSERT INTO analytics_events(project_id, user_id, name, props, release) "
            "VALUES(?,NULL,?,?,?)",
            project_id, name, props, release);
    }
}

std::vector<Count> summary(long project_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT name, count(*) AS c FROM analytics_events WHERE project_id=? "
        "GROUP BY name ORDER BY c DESC, name ASC",
        project_id);
    std::vector<Count> out;
    for (const auto& r : rows) out.push_back({r["name"].as<std::string>(), r["c"].as<long>()});
    return out;
}

std::vector<ReleaseCount> summary_by_release(long project_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT release, name, count(*) AS c FROM analytics_events WHERE project_id=? "
        "GROUP BY release, name ORDER BY release ASC, c DESC, name ASC",
        project_id);
    std::vector<ReleaseCount> out;
    for (const auto& r : rows)
        out.push_back({r["release"].as<std::string>(), r["name"].as<std::string>(),
                       r["c"].as<long>()});
    return out;
}

}  // namespace web::analytics
