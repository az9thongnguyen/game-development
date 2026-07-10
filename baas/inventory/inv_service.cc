// =============================================================================
//  baas/inventory/inv_service.cc  —  see inv_service.h
// =============================================================================
#include "baas/inventory/inv_service.h"

#include <cctype>

#include "baas/db/db.h"

namespace web::inv {
namespace {
constexpr long long kMaxAmount = 1'000'000'000'000LL;   // per-op bound

Error* validate(const std::string& item, long long amount, Error& scratch) {
    if (!valid_item(item)) { scratch = {400, "invalid_item", "item must be 1-64 chars of [A-Za-z0-9_-]"}; return &scratch; }
    if (amount <= 0 || amount > kMaxAmount) { scratch = {400, "invalid_amount", "amount must be 1..1e12"}; return &scratch; }
    return nullptr;
}
}  // namespace

bool valid_item(const std::string& item) {
    if (item.empty() || item.size() > 64) return false;
    for (char c : item)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'))
            return false;
    return true;
}

Item get(long project_id, long user_id, const std::string& item) {
    const auto rows = db::client()->execSqlSync(
        "SELECT qty FROM inventory WHERE project_id=? AND user_id=? AND item=?",
        project_id, user_id, item);
    return Item{item, rows.empty() ? 0 : rows[0]["qty"].as<long>()};
}

std::vector<Item> list(long project_id, long user_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT item, qty FROM inventory WHERE project_id=? AND user_id=? ORDER BY item ASC",
        project_id, user_id);
    std::vector<Item> out;
    for (const auto& r : rows) out.push_back({r["item"].as<std::string>(), r["qty"].as<long>()});
    return out;
}

Result grant(long project_id, long user_id, const std::string& item, long long amount) {
    Error scratch;
    if (Error* e = validate(item, amount, scratch)) return {std::nullopt, *e};

    auto       db  = db::client();
    const auto ex  = db->execSqlSync(
        "SELECT qty FROM inventory WHERE project_id=? AND user_id=? AND item=?",
        project_id, user_id, item);
    long long qty;
    if (ex.empty()) {
        qty = amount;
        db->execSqlSync("INSERT INTO inventory(project_id, user_id, item, qty) VALUES(?,?,?,?)",
                        project_id, user_id, item, qty);
    } else {
        qty = ex[0]["qty"].as<long>() + amount;
        db->execSqlSync(
            "UPDATE inventory SET qty=?, updated_at=CURRENT_TIMESTAMP "
            "WHERE project_id=? AND user_id=? AND item=?",
            qty, project_id, user_id, item);
    }
    return {Item{item, qty}, std::nullopt};
}

Result consume(long project_id, long user_id, const std::string& item, long long amount) {
    Error scratch;
    if (Error* e = validate(item, amount, scratch)) return {std::nullopt, *e};

    auto            db  = db::client();
    const auto      ex  = db->execSqlSync(
        "SELECT qty FROM inventory WHERE project_id=? AND user_id=? AND item=?",
        project_id, user_id, item);
    const long long cur = ex.empty() ? 0 : ex[0]["qty"].as<long>();
    if (cur < amount)
        return {std::nullopt, Error{409, "insufficient", "not enough " + item}};

    const long long qty = cur - amount;
    db->execSqlSync(
        "UPDATE inventory SET qty=?, updated_at=CURRENT_TIMESTAMP "
        "WHERE project_id=? AND user_id=? AND item=?",
        qty, project_id, user_id, item);
    return {Item{item, qty}, std::nullopt};
}

}  // namespace web::inv
