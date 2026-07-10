// =============================================================================
//  baas/leaderboard/lb_service.cc  —  see lb_service.h
// =============================================================================
#include "baas/leaderboard/lb_service.h"

#include "baas/db/db.h"

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
        "SELECT id, sort FROM leaderboards WHERE project_id=? AND key=?", project_id, key);
    if (rows.empty()) return std::nullopt;
    return Board{rows[0]["id"].as<long>(), rows[0]["sort"].as<std::string>() != "asc"};
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
        if (better) {
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
