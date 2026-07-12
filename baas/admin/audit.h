// =============================================================================
//  baas/admin/audit.h  —  append-only audit log (H2 RBAC/audit foundation)
// =============================================================================
//  "Who changed what, when." Mutating admin/operator actions call record(); the
//  dashboard and post-incident review read them back with recent(). Backed by the
//  audit_log table (schema migration 2). This is the auditing half of the H2
//  RBAC/audit item — roles and short-lived credentials are separate later slices.
//  ponytail: no severity levels, no structured detail schema, no retention policy —
//  a flat action+detail row is what a solo operator actually reads. Add structure
//  when a real query needs it.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace web::audit {

struct Entry {
    long        id;
    long        project_id;   // 0 for platform-level actions
    std::string actor;        // "admin", or an operator identity once RBAC lands
    std::string action;       // dotted verb, e.g. "project.create"
    std::string detail;       // free-form context (a name, a key, a reason)
    std::string created_at;
};

// Append one action. project_id == 0 records a platform-level action (stored NULL).
// Uses the process-wide db::client(); a no-op-safe write (never throws to the caller
// path it decorates — auditing must not break the action it records).
void record(long project_id, const std::string& actor, const std::string& action,
            const std::string& detail);

// Most-recent-first entries for a project (project_id == 0 → platform-level rows).
std::vector<Entry> recent(long project_id, int limit);

}  // namespace web::audit
