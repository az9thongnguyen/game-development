// =============================================================================
//  baas/inventory/inv_service.cc  —  see inv_service.h
// =============================================================================
#include "baas/inventory/inv_service.h"

#include <cctype>

#include "baas/common/idempotency.h"
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

// The idempotency store lived HERE until chapter 139, with a note saying to move it
// to baas/common when a SECOND endpoint needed it. The match-result endpoint is that
// second endpoint, so it moved.

namespace {

// Read a quantity inside a transaction, LOCKING the row on a backend that has row
// locks. Every read-then-write in this file goes through it, because the alternative
// is three copies of one decision and two of them eventually forgotten.
//
// `db::lock_clause()` is "" on SQLite — where a transaction already holds the only
// connection in the pool — and " FOR UPDATE" on Postgres, where it does not.
long long qty_locked(const std::shared_ptr<drogon::orm::Transaction>& tx, long project_id,
                     long user_id, const std::string& item) {
    const auto rows = db::exec(tx,
        std::string("SELECT qty FROM inventory WHERE project_id=? AND user_id=? AND item=?") +
            db::lock_clause(),
        project_id, user_id, item);
    return rows.empty() ? 0 : rows[0]["qty"].as<long>();
}

// ...and the same read, WITHOUT a lock, for a caller that is only answering a
// question. Named apart so a read-then-write cannot reach for it by accident.
long long qty_of(long project_id, long user_id, const std::string& item) {
    const auto rows = db::exec(db::client(),
        "SELECT qty FROM inventory WHERE project_id=? AND user_id=? AND item=?",
        project_id, user_id, item);
    return rows.empty() ? 0 : rows[0]["qty"].as<long>();
}

// Write a quantity that has already been decided, inside `tx`.
void put_qty(const std::shared_ptr<drogon::orm::Transaction>& tx, long project_id,
             long user_id, const std::string& item, long long qty, bool exists) {
    if (exists)
        db::exec(tx, "UPDATE inventory SET qty=?, updated_at=CURRENT_TIMESTAMP "
                        "WHERE project_id=? AND user_id=? AND item=?",
                        qty, project_id, user_id, item);
    else
        db::exec(tx, "INSERT INTO inventory(project_id, user_id, item, qty) VALUES(?,?,?,?)",
                        project_id, user_id, item, qty);
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
    return Item{item, qty_of(project_id, user_id, item)};
}

std::vector<Item> list(long project_id, long user_id) {
    const auto rows = db::exec(db::client(),
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
        if (auto prior = idem::lookup(project_id, scoped_key))
            return {Item{item, *prior}, std::nullopt};
    }

    // A TRANSACTION, since chapter 140. Before it this was a bare read followed by a
    // bare write, and the comment in `purchase` below — "atomic because the SQLite
    // pool is size 1" — was true only of code that opens a transaction. Two threads
    // calling `execSqlSync` twice each interleave on a pool of one just as happily as
    // on a pool of ten: the connection is held for one STATEMENT, not for a sequence.
    // So two concurrent grants of 5 to an empty slot could both read 0 and both write
    // 5, and the player was owed 10.
    auto tx = db::client()->newTransaction();
    try {
        const auto ex  = db::exec(tx,
            std::string("SELECT qty FROM inventory WHERE project_id=? AND user_id=? AND item=?") +
                db::lock_clause(),
            project_id, user_id, item);
        const long long qty = (ex.empty() ? 0 : ex[0]["qty"].as<long>()) + amount;
        put_qty(tx, project_id, user_id, item, qty, !ex.empty());
        // Inside the transaction, so the key commits with the effect it describes.
        if (!scoped_key.empty()) idem::record_with(tx, project_id, scoped_key, qty);
        return {Item{item, qty}, std::nullopt};
    } catch (const std::exception&) {
        tx->rollback();
        return {std::nullopt, Error{500, "internal", "grant failed"}};
    }
}

Result purchase(long project_id, long user_id, const std::string& currency, long long cost,
                const std::string& item, long long amount, const std::string& idem_key) {
    Error scratch;
    if (Error* e = validate(currency, cost, scratch)) return {std::nullopt, *e};
    if (Error* e = validate(item, amount, scratch))   return {std::nullopt, *e};

    // Scope the key to (user, this purchase's item) with a "purchase|" tag so it can never
    // collide with a grant's key or another item's purchase key (see grant for the rationale).
    const std::string scoped_key =
        idem_key.empty() ? std::string()
                         : "purchase|" + std::to_string(user_id) + "|" + item + "|" + idem_key;
    if (!scoped_key.empty()) {
        if (auto prior = idem::lookup(project_id, scoped_key))
            return {Item{item, *prior}, std::nullopt};   // replay — no second purchase
    }

    // One transaction: check funds → spend → grant → record the key. It commits when `tx`
    // is destroyed; any error path calls rollback() so nothing partial is left behind.
    //
    // The affordability check reads the balance and then writes it, and until chapter
    // 140 the comment here said that was atomic "because the SQLite pool is size 1".
    // That was true, and it was true for a reason that stops being true the moment
    // anybody points this at Postgres — which is the whole plan. `qty_locked` asks for
    // the row lock, and asks for it in the only place that knows whether the backend
    // has one.
    auto tx = db::client()->newTransaction();
    try {
        const long long have = qty_locked(tx, project_id, user_id, currency);
        if (have < cost) {
            tx->rollback();
            return {std::nullopt, Error{409, "insufficient", "not enough " + currency}};
        }

        // Spend the currency.
        db::exec(tx,
            "UPDATE inventory SET qty=?, updated_at=CURRENT_TIMESTAMP "
            "WHERE project_id=? AND user_id=? AND item=?",
            have - cost, project_id, user_id, currency);

        // Grant the item (upsert), computing its resulting quantity. Locked for the
        // same reason: the currency and the item are two different rows.
        const auto ex = db::exec(tx,
            std::string("SELECT qty FROM inventory WHERE project_id=? AND user_id=? AND item=?") +
                db::lock_clause(),
            project_id, user_id, item);
        const long long qty = (ex.empty() ? 0 : ex[0]["qty"].as<long>()) + amount;
        put_qty(tx, project_id, user_id, item, qty, !ex.empty());

        // Record idempotency INSIDE the transaction so the key commits atomically with the
        // spend+grant — a retry cannot land between the effect and the record.
        if (!scoped_key.empty())
            idem::record_with(tx,
                project_id, scoped_key, qty);

        return {Item{item, qty}, std::nullopt};   // tx commits on scope exit
    } catch (const std::exception&) {
        tx->rollback();
        return {std::nullopt, Error{500, "internal", "purchase failed"}};
    }
}

Result consume(long project_id, long user_id, const std::string& item, long long amount) {
    Error scratch;
    if (Error* e = validate(item, amount, scratch)) return {std::nullopt, *e};

    // Same shape as grant, and the same fix — except this one could go NEGATIVE, which
    // is the version of a lost update a player notices.
    auto tx = db::client()->newTransaction();
    try {
        const long long cur = qty_locked(tx, project_id, user_id, item);
        if (cur < amount) {
            tx->rollback();
            return {std::nullopt, Error{409, "insufficient", "not enough " + item}};
        }
        const long long qty = cur - amount;
        put_qty(tx, project_id, user_id, item, qty, /*exists=*/true);
        return {Item{item, qty}, std::nullopt};
    } catch (const std::exception&) {
        tx->rollback();
        return {std::nullopt, Error{500, "internal", "consume failed"}};
    }
}

}  // namespace web::inv
