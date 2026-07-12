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

// A per-release, per-event tally — the shape that lets an operator compare the health
// and usage of one release against another ("measures a release", H2 exit gate).
struct ReleaseCount {
    std::string release;   // "" for events with no release attribution
    std::string name;
    long long   count;
};

bool valid_name(const std::string& name);   // 1-64 chars of [A-Za-z0-9_.-]

// user_id 0 → anonymous (stored as NULL). props is an opaque JSON string. `release` is
// the client's release id (e.g. the release-store content hash) or "" if unattributed.
void record(long project_id, long user_id, const std::string& name, const std::string& props,
            const std::string& release = "");

// admin (dashboard): event counts by name, most frequent first.
std::vector<Count> summary(long project_id);

// admin (dashboard): event counts grouped by (release, name) — release rows ordered,
// most frequent event first within each. This is how a release is "measured".
std::vector<ReleaseCount> summary_by_release(long project_id);

}  // namespace web::analytics
