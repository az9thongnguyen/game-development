// =============================================================================
//  baas/leaderboard/lb_service.cc  —  see lb_service.h
// =============================================================================
#include "baas/leaderboard/lb_service.h"

#include "baas/common/idempotency.h"
#include "baas/db/db.h"
#include "engine/elo.hpp"

namespace web::lb {
namespace {

// The comparison operator for "strictly better" given the sort direction. This
// is a fixed literal chosen by a bool — never user input — so concatenating it
// into the SQL is injection-safe (the value is still bound as a parameter).
const char* better_than(bool desc) { return desc ? ">" : "<"; }

int rank_for_value(const Board& b, long value) {
    const auto rows = db::client()->execSqlSync(
        std::string("SELECT count(*) AS c FROM scores WHERE leaderboard_id=? AND value ") +
            better_than(b.desc) + " ?",
        b.id, value);
    return static_cast<int>(rows[0]["c"].as<long>()) + 1;
}

}  // namespace

std::optional<Board> find_board(long project_id, const std::string& key) {
    const auto rows = db::client()->execSqlSync(
        "SELECT id, sort, mode FROM leaderboards WHERE project_id=? AND key=?", project_id, key);
    if (rows.empty()) return std::nullopt;
    return Board{rows[0]["id"].as<long>(),
                 rows[0]["sort"].as<std::string>() != "asc",
                 rows[0]["mode"].as<std::string>() != "last"};
}

SubmitResult submit(const Board& board, long user_id, long value) {
    auto db = db::client();
    const auto existing = db->execSqlSync(
        "SELECT value FROM scores WHERE leaderboard_id=? AND user_id=?", board.id, user_id);

    bool updated     = false;
    long final_value = value;

    if (existing.empty()) {
        db->execSqlSync("INSERT INTO scores(leaderboard_id, user_id, value) VALUES(?,?,?)",
                        board.id, user_id, value);
        updated = true;
    } else {
        const long old    = existing[0]["value"].as<long>();
        const bool better = board.desc ? (value > old) : (value < old);
        // `keep_best` is the only thing that reads the sort here. A 'last' board
        // stores what it was handed — including a lower number, which is the entire
        // reason the column exists.
        if (better || !board.keep_best) {
            db->execSqlSync(
                "UPDATE scores SET value=?, updated_at=CURRENT_TIMESTAMP "
                "WHERE leaderboard_id=? AND user_id=?",
                value, board.id, user_id);
            updated = true;
        } else {
            final_value = old;   // keep the better existing value
        }
    }
    return {rank_for_value(board, final_value), final_value, updated};
}

namespace {
// The stored rating, or where a player starts. A ladder whose first game was against
// "no rating" would have to invent one anyway; inventing it here keeps every path
// through the arithmetic identical.
long rating_of(const Board& board, long user_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT value FROM scores WHERE leaderboard_id=? AND user_id=?", board.id, user_id);
    return rows.empty() ? engine::kEloStart : rows[0]["value"].as<long>();
}

void store_rating(const Board& board, long user_id, long value) {
    auto db = db::client();
    const auto rows = db->execSqlSync(
        "SELECT 1 AS x FROM scores WHERE leaderboard_id=? AND user_id=?", board.id, user_id);
    if (rows.empty())
        db->execSqlSync("INSERT INTO scores(leaderboard_id, user_id, value) VALUES(?,?,?)",
                        board.id, user_id, value);
    else
        db->execSqlSync("UPDATE scores SET value=?, updated_at=CURRENT_TIMESTAMP "
                        "WHERE leaderboard_id=? AND user_id=?",
                        value, board.id, user_id);
}
}  // namespace

bool user_in_project(long project_id, long user_id) {
    return !db::client()
                ->execSqlSync("SELECT 1 AS x FROM users WHERE id=? AND project_id=?",
                              user_id, project_id)
                .empty();
}

MatchResult apply_match(long project_id, const Board& board, long user_id, long opponent_id,
                        int result, const std::string& match_key) {
    const std::string key = "match:" + std::to_string(board.id) + ":" + match_key;

    if (idem::lookup(project_id, key)) {
        // Already reported — by the other player, almost always. Return where this
        // reporter actually stands rather than the stored number, which belongs to
        // whoever got here first.
        // `delta` is deliberately 0 here rather than the stored one: the stored
        // delta belongs to whoever reported FIRST, and handing it to the second
        // reporter told the loser of the first real match that they had gained
        // sixteen points. A number that is right for someone else is worse than no
        // number.
        const long mine = rating_of(board, user_id);
        return {mine, rating_of(board, opponent_id), rank_for_value(board, mine), 0, false};
    }

    const long mine_before  = rating_of(board, user_id);
    const long their_before = rating_of(board, opponent_id);

    const int score = result > 0 ? engine::kEloWin
                                 : (result < 0 ? engine::kEloLoss : engine::kEloDraw);
    const long mine_after = engine::elo_update(static_cast<int>(mine_before),
                                               static_cast<int>(their_before), score);
    // The OPPOSITE score, not the opposite delta: computing one and negating it would
    // work today and stop working the moment the two players ever get different K.
    const long their_after = engine::elo_update(static_cast<int>(their_before),
                                                static_cast<int>(mine_before),
                                                engine::kEloWin - score);

    store_rating(board, user_id, mine_after);
    store_rating(board, opponent_id, their_after);
    idem::record(project_id, key, static_cast<long long>(mine_after - mine_before));

    return {mine_after, their_after, rank_for_value(board, mine_after),
            static_cast<int>(mine_after - mine_before), true};
}

std::vector<Entry> top(const Board& board, int limit) {
    const std::string order = board.desc ? "DESC" : "ASC";
    const auto        rows  = db::client()->execSqlSync(
        "SELECT s.user_id, u.display_name, s.value FROM scores s "
        "JOIN users u ON u.id = s.user_id WHERE s.leaderboard_id=? "
        "ORDER BY s.value " + order + ", s.updated_at ASC LIMIT ?",
        board.id, limit);

    std::vector<Entry> out;
    int                rank = 1;
    for (const auto& r : rows)
        out.push_back({rank++, r["user_id"].as<long>(),
                       r["display_name"].as<std::string>(), r["value"].as<long>()});
    return out;
}

std::optional<Entry> rank_of(const Board& board, long user_id) {
    const auto mine = db::client()->execSqlSync(
        "SELECT s.value, u.display_name FROM scores s JOIN users u ON u.id = s.user_id "
        "WHERE s.leaderboard_id=? AND s.user_id=?",
        board.id, user_id);
    if (mine.empty()) return std::nullopt;

    const long        value = mine[0]["value"].as<long>();
    const std::string name  = mine[0]["display_name"].as<std::string>();
    return Entry{rank_for_value(board, value), user_id, name, value};
}

}  // namespace web::lb
