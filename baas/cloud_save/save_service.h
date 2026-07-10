// =============================================================================
//  baas/cloud_save/save_service.h  —  per-user cloud save (put/get/list/remove)
// =============================================================================
//  A named-slot key/value store scoped to (project_id, user_id). Payloads are
//  opaque UTF-8 text (the game owns their meaning). Optimistic concurrency via a
//  version that bumps on every write; a caller may require the current version to
//  match (If-Match) to avoid clobbering a newer save.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace web::save {

struct Meta {
    std::string slot;
    long long   version = 0;
    long long   size    = 0;   // payload byte length
    std::string updated_at;
};

struct Record {
    std::string slot;
    long long   version = 0;
    std::string data;
    std::string updated_at;
};

struct Error {
    int         status;
    std::string code;
    std::string message;
};

struct PutResult {
    std::optional<Meta>  meta;
    std::optional<Error> error;
};

// non-empty, ≤ 64 chars, [A-Za-z0-9_-] only
bool valid_slot(const std::string& slot);

// Upsert the slot. if_match == 0 → no check (last-write-wins); if_match > 0 → the
// current version must equal it, else 409. Returns the new Meta (size in bytes).
PutResult put(long project_id, long user_id, const std::string& slot,
              const std::string& data, long long if_match);

std::optional<Record> get(long project_id, long user_id, const std::string& slot);
std::vector<Meta>     list(long project_id, long user_id);
bool                  remove(long project_id, long user_id, const std::string& slot);

}  // namespace web::save
