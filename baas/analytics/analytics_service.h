// =============================================================================
//  baas/analytics/analytics_service.h  —  fire-and-forget event ingest
// =============================================================================
//  Games POST gameplay events; the server just records them (project-scoped, with
//  an optional user_id). Querying/aggregation is an admin/dashboard concern (L3).
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace web::analytics {

struct Count {
    std::string name;
    long long   count;
};

bool valid_name(const std::string& name);   // 1-64 chars of [A-Za-z0-9_.-]

// user_id 0 → anonymous (stored as NULL). props is an opaque JSON string.
void record(long project_id, long user_id, const std::string& name, const std::string& props);

// admin (dashboard): event counts by name, most frequent first.
std::vector<Count> summary(long project_id);

}  // namespace web::analytics
