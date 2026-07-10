// =============================================================================
//  baas/live_events/events_service.h  —  currently-active live events (read)
// =============================================================================
//  Time-boxed server-driven events (sales, double-XP weekends). Client-facing L1
//  reads the ones active *now*; scheduling/creating them is a dashboard (L3)
//  admin action.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace web::live {

struct Event {
    std::string key;
    std::string name;
    std::string payload;   // opaque JSON
};

std::vector<Event> active(long project_id);   // starts_at <= now <= ends_at

// admin (dashboard): schedule/replace an event.
void create(long project_id, const std::string& key, const std::string& name,
            const std::string& starts_at, const std::string& ends_at, const std::string& payload);

}  // namespace web::live
