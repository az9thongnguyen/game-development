// =============================================================================
//  baas/common/idempotency.h  —  "this operation already happened"
// =============================================================================
//  A tiny store over the `idempotency_keys` table (migration 4): a project-scoped
//  key, and the result the first caller got. A retry finds the key and returns that
//  result instead of applying the effect again.
//
//  It lived inside `inv_service.cc` with a note on it:
//
//      when a SECOND endpoint needs idempotency, graduate these to
//      baas/common/idempotency.
//
//  Chapter 139 is that second endpoint. A match result is reported by BOTH players
//  — that is not a retry, it is the normal case — so applying it twice would move
//  every rating twice.
//
//  ponytail: lookup-then-record still has a hair-thin double-apply window under two
//  CONCURRENT first uses of one key. Single-writer SQLite serialises execSqlSync, so
//  it is effectively closed today; a multi-writer backend (Postgres) needs a
//  claim-first INSERT. Stated, not built — same as when it lived next door.
// =============================================================================
#pragma once

#include <optional>
#include <string>

#include <drogon/orm/DbClient.h>

namespace web::idem {

// The result recorded for `key`, or nullopt if this is the first time.
std::optional<long long> lookup(long project_id, const std::string& key);

// Record it, inside the caller's Transaction, so the key commits atomically with the
// effect it describes. A retry cannot then land between the two. Typed on Transaction
// rather than DbClient since chapter 141: `db::Transaction` converts to one implicitly,
// and a chain of two user-defined conversions does not exist in C++.
void record_with(const std::shared_ptr<drogon::orm::Transaction>& db, long project_id,
                 const std::string& key, long long result);

}  // namespace web::idem
