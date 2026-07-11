// =============================================================================
//  baas/test_runner/testrun_service.cc  —  see testrun_service.h
// =============================================================================
#include "baas/test_runner/testrun_service.h"

#include "baas/db/db.h"

namespace web::testrun {

bool valid_status(const std::string& s) {
    return s == "passed" || s == "failed" || s == "error";
}

namespace {
Record row_to_record(const drogon::orm::Row& r) {
    return Record{r["id"].as<long long>(),      r["scenario"].as<std::string>(),
                  r["params"].as<std::string>(), r["status"].as<std::string>(),
                  r["result"].as<std::string>(), r["created_at"].as<std::string>(),
                  r["updated_at"].as<std::string>()};
}
}  // namespace

long long create(long project_id, const std::string& scenario, const std::string& params) {
    const auto r = db::client()->execSqlSync(
        "INSERT INTO testruns(project_id, scenario, params, status) VALUES(?,?,?,'pending')",
        project_id, scenario, params);
    return static_cast<long long>(r.insertId());
}

std::optional<Record> get(long project_id, long long id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT id, scenario, params, status, result, created_at, updated_at "
        "FROM testruns WHERE project_id=? AND id=?",
        project_id, id);
    if (rows.empty()) return std::nullopt;
    return row_to_record(rows[0]);
}

std::vector<Record> list(long project_id, const std::string& status_filter) {
    const char* sql_all =
        "SELECT id, scenario, params, status, result, created_at, updated_at "
        "FROM testruns WHERE project_id=? ORDER BY id ASC";
    const char* sql_st =
        "SELECT id, scenario, params, status, result, created_at, updated_at "
        "FROM testruns WHERE project_id=? AND status=? ORDER BY id ASC";
    const auto rows = status_filter.empty()
        ? db::client()->execSqlSync(sql_all, project_id)
        : db::client()->execSqlSync(sql_st, project_id, status_filter);
    std::vector<Record> out;
    for (const auto& r : rows) out.push_back(row_to_record(r));
    return out;
}

bool claim(long project_id, long long id) {
    // Atomic: only a pending run flips to running, so two workers can't both win it.
    const auto r = db::client()->execSqlSync(
        "UPDATE testruns SET status='running', updated_at=CURRENT_TIMESTAMP "
        "WHERE project_id=? AND id=? AND status='pending'",
        project_id, id);
    return r.affectedRows() > 0;
}

bool complete(long project_id, long long id, const std::string& status, const std::string& result) {
    // Only a claimed (running) run can be completed.
    const auto r = db::client()->execSqlSync(
        "UPDATE testruns SET status=?, result=?, updated_at=CURRENT_TIMESTAMP "
        "WHERE project_id=? AND id=? AND status='running'",
        status, result, project_id, id);
    return r.affectedRows() > 0;
}

}  // namespace web::testrun
