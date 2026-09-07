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
    bool desc;        // true: higher value ranks first; false: lower value ranks first
    // What a RESUBMISSION means. A high score keeps the better of the two; a RATING
    // must be able to go down, and a board that quietly refused to lower it would be
    // a record of everybody's best day rather than a ladder. Defaulted to `true` so a
    // caller that forgets gets the older, safer behaviour rather than a board that
    // silently overwrites.
    bool keep_best = true;
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

// ---- a rated match ----------------------------------------------------------
//
//  A leaderboard is a place a client PUTS a number. A ladder cannot be: whoever
//  reports the match would be choosing their own rating, and the anti-spoof rule
//  this project already keeps everywhere else ("the score belongs to the JWT's user,
//  never a body field") would mean nothing if the VALUE were still a body field.
//
//  So the server owns the arithmetic. It reads both ratings, applies the same
//  integer Elo the client uses to PREDICT the outcome (engine/elo.hpp — shared, not
//  copied, because two spellings of one curve is exactly the bug it exists to
//  prevent), and writes both.
//
//  Reported by BOTH players, which is not a retry but the normal case, so `match_key`
//  makes it idempotent: the second report returns the first one's result and moves
//  nothing.
struct MatchResult {
    long value           = 0;   // the reporter's rating after the match
    long opponent_value  = 0;
    int  rank            = 0;
    int  delta           = 0;   // what this match was worth to the reporter
    bool applied         = false;  // false = this match had already been reported
};

// `result` is +1 win, 0 draw, -1 loss, from the REPORTER's point of view.
// `match_key` must be non-empty; it is scoped to the board inside.
MatchResult apply_match(long project_id, const Board& board, long user_id,
                        long opponent_id, int result, const std::string& match_key);

// Is this user a player in this project? The match endpoint's tenant check: without
// it a reporter could name any integer and mint a rating row for it.
bool user_in_project(long project_id, long user_id);

}  // namespace web::lb
