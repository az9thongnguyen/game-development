// =============================================================================
//  baas/replays/replay_service.h  —  per-user replay store (create/get/list/remove)
// =============================================================================
//  A replay is an immutable, named blob owned by (project_id, user_id): a recorded
//  stream a game can play back (e.g. a command/input log for a deterministic sim).
//  Unlike a cloud save (one overwritable slot), a user keeps MANY replays, each
//  with a server-assigned id. Payloads are opaque UTF-8 text — the game owns their
//  meaning. Everything is scoped by project + user.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace web::replay {

struct Meta {
    long long   id   = 0;
    std::string name;
    long long   size = 0;   // payload byte length
    std::string created_at;
};

struct Record {
    long long   id = 0;
    std::string name;
    std::string data;
    std::string created_at;
};

// non-empty, ≤ 64 chars, printable ASCII (0x20–0x7E) — a human label
bool valid_name(const std::string& name);

// Insert a new replay; returns its server-assigned id.
long long create(long project_id, long user_id, const std::string& name, const std::string& data);

std::optional<Record> get(long project_id, long user_id, long long id);
std::vector<Meta>     list(long project_id, long user_id);   // newest first
bool                  remove(long project_id, long user_id, long long id);

}  // namespace web::replay
