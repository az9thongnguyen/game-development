// =============================================================================
//  baas/store/store_service.h  —  priced catalog + buy-by-SKU (H2 economy)
// =============================================================================
//  A catalog of priced offers: a SKU maps to a price (spend `cost` of `currency`)
//  and a reward (grant `amount` of `item`). The server owns prices, so a client buys
//  a SKU, never an arbitrary cost. `buy` resolves the SKU and delegates to the atomic
//  inv::purchase, so the spend+grant is all-or-nothing and idempotent (see ch.103).
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "baas/inventory/inv_service.h"

namespace web::store {

struct Offer {
    std::string sku;
    std::string currency;
    long long   cost = 0;
    std::string item;
    long long   amount = 0;
};

bool valid_sku(const std::string& sku);   // 1-64 chars of [A-Za-z0-9_.-]

std::optional<Offer> get(long project_id, const std::string& sku);
std::vector<Offer>   list(long project_id);

// admin (dashboard): define or update an offer. Audited. Returns false on a bad SKU or
// a non-positive cost/amount (an offer must charge something and grant something).
bool upsert(long project_id, const std::string& sku, const std::string& currency,
            long long cost, const std::string& item, long long amount, const std::string& actor);

// Buy `sku` for `user_id`: look up the offer and atomically spend its price + grant its
// reward via inv::purchase. Returns a 404 "unknown_sku" if the SKU is not in the catalog,
// otherwise whatever inv::purchase returns (incl. 409 "insufficient"). `idem_key` makes a
// retry safe (no double purchase).
inv::Result buy(long project_id, long user_id, const std::string& sku,
                const std::string& idem_key = "");

}  // namespace web::store
