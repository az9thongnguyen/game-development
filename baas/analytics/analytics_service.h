// =============================================================================
//  baas/analytics/analytics_service.h  —  fire-and-forget event ingest
// =============================================================================
//  Games POST gameplay events; the server just records them (project-scoped, with
//  an optional user_id). Querying/aggregation is an admin/dashboard concern (L3).
// =============================================================================
#pragma once

#include <string>

namespace web::analytics {

bool valid_name(const std::string& name);   // 1-64 chars of [A-Za-z0-9_.-]

// user_id 0 → anonymous (stored as NULL). props is an opaque JSON string.
void record(long project_id, long user_id, const std::string& name, const std::string& props);

}  // namespace web::analytics
