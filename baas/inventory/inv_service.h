// =============================================================================
//  baas/inventory/inv_service.h  —  per-user item quantities (grant/consume)
// =============================================================================
//  Scoped to (project_id, user_id). grant adds; consume subtracts but is
//  server-enforced to never go negative (a client cannot over-spend). get returns
//  0 for a never-held item — inventory is a count, not a presence check.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace web::inv {

struct Item {
    std::string item;
    long long   qty = 0;
};

struct Error {
    int         status;
    std::string code;
    std::string message;
};

struct Result {
    std::optional<Item>  item;
    std::optional<Error> error;
};

bool valid_item(const std::string& item);   // 1-64 chars of [A-Za-z0-9_-]

Item              get(long project_id, long user_id, const std::string& item);
std::vector<Item> list(long project_id, long user_id);
Result            grant(long project_id, long user_id, const std::string& item, long long amount);
Result            consume(long project_id, long user_id, const std::string& item, long long amount);

}  // namespace web::inv
