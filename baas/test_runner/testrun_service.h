// =============================================================================
//  baas/test_runner/testrun_service.h  —  the test-run job registry (coordinator)
// =============================================================================
//  A project-scoped queue of headless test-runs. The BaaS only COORDINATES:
//  create a run (pending), a worker claims it (pending→running, atomic) and
//  completes it (running→passed/failed/error). The BaaS never executes anything
//  — running links no engine code (see docs/book/82). Mirrors asset_service.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace web::testrun {

struct Record {
    long long   id = 0;
    std::string scenario;
    std::string params;
    std::string status;     // pending|running|passed|failed|error
    std::string result;
    std::string created_at;
    std::string updated_at;
};

bool valid_status(const std::string& s);   // a worker may only set passed|failed|error

long long             create(long project_id, const std::string& scenario, const std::string& params);
std::optional<Record> get(long project_id, long long id);
std::vector<Record>   list(long project_id, const std::string& status_filter);  // "" = all
bool                  claim(long project_id, long long id);      // pending -> running (atomic)
bool                  complete(long project_id, long long id,
                               const std::string& status, const std::string& result);  // running -> done

}  // namespace web::testrun
