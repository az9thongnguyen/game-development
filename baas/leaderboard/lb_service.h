// =============================================================================
//  baas/leaderboard/lb_service.h  —  leaderboard logic (ranking + best-upsert)
// =============================================================================
//  Service over the DbClient. A board is resolved once (find_board) and passed
//  to the operations, which keep the query logic focused. All are scoped to a
//  project via the board id. Ranking: rank = (# strictly-better scores) + 1.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace web::lb {

struct Board {
    long id;
    bool desc;   // true: higher value ranks first; false: lower value ranks first
};

struct Entry {
    int         rank;
    long        user_id;
    std::string display_name;
    long        value;
};

struct SubmitResult {
    int  rank;
    long value;     // the value now on the board (the better of old/new)
    bool updated;   // did this submission change the stored value?
};

// Resolve (project_id, key) → board, or nullopt if the project has no such board.
std::optional<Board> find_board(long project_id, const std::string& key);

// Upsert keeping the better value per the board's sort. Returns the resulting
// rank/value and whether the stored value changed.
SubmitResult submit(const Board& board, long user_id, long value);

// Top N entries, best-first (ties broken by earliest to reach the value).
std::vector<Entry> top(const Board& board, int limit);

// This user's rank+value, or nullopt if they have no score yet.
std::optional<Entry> rank_of(const Board& board, long user_id);

}  // namespace web::lb
