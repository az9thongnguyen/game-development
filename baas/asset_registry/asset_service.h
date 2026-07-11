// =============================================================================
//  baas/asset_registry/asset_service.h  —  project-scoped asset registry
// =============================================================================
//  A named key/value store scoped to a PROJECT (not a user): every player of a
//  game sees the same assets. Payloads are opaque UTF-8 text (binary assets like
//  .hrt are base64-encoded by the caller). `kind` is a free tag used only for list
//  filtering. Optimistic concurrency via a version that bumps on every write.
//  Mirrors cloud_save (baas/cloud_save/save_service.h) minus the per-user scope.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace web::asset {

struct Meta {
    std::string name;
    std::string kind;
    long long   version = 0;
    long long   size    = 0;   // payload byte length
    std::string updated_at;
};

struct Record {
    std::string name;
    std::string kind;
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

bool valid_name(const std::string& name);   // 1..128 chars, [A-Za-z0-9._-]
bool valid_kind(const std::string& kind);   // 0..32 chars, [A-Za-z0-9_-] (empty allowed)

// Upsert (project_id, name). if_match == 0 → last-write-wins; if_match > 0 → the
// current version must equal it, else 409. Returns the new Meta (size in bytes).
PutResult put(long project_id, const std::string& name, const std::string& kind,
              const std::string& data, long long if_match);

std::optional<Record> get(long project_id, const std::string& name);
std::vector<Meta>     list(long project_id, const std::string& kind_filter);  // "" = all
bool                  remove(long project_id, const std::string& name);

}  // namespace web::asset
