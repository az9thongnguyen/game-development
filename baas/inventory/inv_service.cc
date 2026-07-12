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

namespace {
// Idempotency store, currently used only by grant. Two tiny helpers over the
// idempotency_keys table (migration 4). ponytail: lookup-then-record has a hair-thin
// double-apply window under two *concurrent* first uses of one key; single-writer SQLite
// serializes execSqlSync so it is effectively closed today. When a multi-writer backend
// (Postgres) is adopted, switch to a claim-first INSERT; when a SECOND endpoint needs
// idempotency, graduate these to baas/common/idempotency.
std::optional<long long> idem_lookup(long project_id, const std::string& key) {
    const auto rows = db::client()->execSqlSync(
        "SELECT result FROM idempotency_keys WHERE project_id=? AND idem_key=?",
        project_id, key);
    if (rows.empty()) return std::nullopt;
    return rows[0]["result"].as<long long>();
}
void idem_record(long project_id, const std::string& key, long long result) {
    // ON CONFLICT DO NOTHING (portable across SQLite ≥3.24 and Postgres): a racing
    // duplicate is a harmless no-op, first writer wins.
    db::client()->execSqlSync(
        "INSERT INTO idempotency_keys(project_id, idem_key, result) VALUES(?,?,?) "
        "ON CONFLICT(project_id, idem_key) DO NOTHING",
        project_id, key, result);
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

Result grant(long project_id, long user_id, const std::string& item, long long amount,
             const std::string& idem_key) {
    Error scratch;
    if (Error* e = validate(item, amount, scratch)) return {std::nullopt, *e};

    // Scope the client key to (user, item): the SAME Idempotency-Key from a different user,
    // or for a different item, must NOT collide and replay the wrong grant. `item` is
    // validated above to [A-Za-z0-9_-] (no '|') and user_id is numeric, so the
    // "<uid>|<item>|" prefix is unambiguous and the arbitrary client key follows it.
    const std::string scoped_key =
        idem_key.empty() ? std::string()
                         : std::to_string(user_id) + "|" + item + "|" + idem_key;

    // Idempotent retry: if this key already produced a result, replay it — do not grant
    // again. (The replayed qty is the item's total after the original grant.)
    if (!scoped_key.empty()) {
        if (auto prior = idem_lookup(project_id, scoped_key))
            return {Item{item, *prior}, std::nullopt};
    }

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
    if (!scoped_key.empty()) idem_record(project_id, scoped_key, qty);   // remember for retries
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
