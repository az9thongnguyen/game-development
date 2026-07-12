// =============================================================================
//  baas/store/store_service.cc  —  see store_service.h
// =============================================================================
#include "baas/store/store_service.h"

#include <cctype>

#include <drogon/orm/Row.h>

#include "baas/admin/audit.h"
#include "baas/db/db.h"

namespace web::store {

bool valid_sku(const std::string& sku) {
    if (sku.empty() || sku.size() > 64) return false;
    for (char c : sku)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
            return false;
    return true;
}

namespace {
Offer row_to_offer(const drogon::orm::Row& r) {
    return Offer{r["sku"].as<std::string>(), r["currency"].as<std::string>(),
                 r["cost"].as<long long>(), r["item"].as<std::string>(),
                 r["amount"].as<long long>()};
}
}  // namespace

std::optional<Offer> get(long project_id, const std::string& sku) {
    const auto rows = db::client()->execSqlSync(
        "SELECT sku, currency, cost, item, amount FROM catalog WHERE project_id=? AND sku=?",
        project_id, sku);
    if (rows.empty()) return std::nullopt;
    return row_to_offer(rows[0]);
}

std::vector<Offer> list(long project_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT sku, currency, cost, item, amount FROM catalog WHERE project_id=? ORDER BY sku ASC",
        project_id);
    std::vector<Offer> out;
    for (const auto& r : rows) out.push_back(row_to_offer(r));
    return out;
}

bool upsert(long project_id, const std::string& sku, const std::string& currency,
            long long cost, const std::string& item, long long amount, const std::string& actor) {
    if (!valid_sku(sku) || !inv::valid_item(currency) || !inv::valid_item(item)) return false;
    if (cost <= 0 || amount <= 0) return false;   // an offer must charge and grant something

    auto       db = db::client();
    const auto ex = db->execSqlSync("SELECT id FROM catalog WHERE project_id=? AND sku=?",
                                    project_id, sku);
    if (ex.empty())
        db->execSqlSync(
            "INSERT INTO catalog(project_id, sku, currency, cost, item, amount) VALUES(?,?,?,?,?,?)",
            project_id, sku, currency, cost, item, amount);
    else
        db->execSqlSync(
            "UPDATE catalog SET currency=?, cost=?, item=?, amount=?, updated_at=CURRENT_TIMESTAMP "
            "WHERE project_id=? AND sku=?",
            currency, cost, item, amount, project_id, sku);

    audit::record(project_id, actor, "catalog.upsert",
                  "sku=" + sku + " cost=" + std::to_string(cost) + " " + currency +
                      " -> " + std::to_string(amount) + " " + item);
    return true;
}

inv::Result buy(long project_id, long user_id, const std::string& sku, const std::string& idem_key) {
    const auto offer = get(project_id, sku);
    if (!offer)
        return {std::nullopt, inv::Error{404, "unknown_sku", "no such catalog entry: " + sku}};
    // The whole spend+grant is the already-tested atomic, idempotent inv::purchase.
    return inv::purchase(project_id, user_id, offer->currency, offer->cost, offer->item,
                         offer->amount, idem_key);
}

}  // namespace web::store
