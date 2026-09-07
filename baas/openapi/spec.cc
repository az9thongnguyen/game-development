// =============================================================================
//  baas/openapi/spec.cc  —  the table. One row per operation, one operation per route.
// =============================================================================
//  The prose a router cannot know: what an endpoint is FOR, which credential opens
//  it, and what its answers mean. Everything else in the document is derived —
//  parameters from the path, operationId from the method and the path, the tag list
//  from first appearance.
//
//  Adding a route without adding a row here turns `test_baas_openapi` red, and so
//  does the reverse. That is the whole design; see openapi.h.
// =============================================================================
#include "baas/openapi/openapi.h"

namespace web::openapi {
namespace {

// The three answers almost every authenticated route can give, spelled once.
const Reply kBadKey{401, "the api key is missing or unknown"};
const Reply kBadToken{401, "the access token is missing, expired or not for this project"};
const Reply kRateLimited{429, "too many requests from this caller"};

}  // namespace

const std::vector<Op>& spec() {
    static const std::vector<Op> ops = {
    // ---- liveness and operations, deliberately outside /v1 -------------------
    {"GET", "/healthz", "health", "Liveness probe. No credential, no database.",
     Auth::None, "", {{200, "the process is up"}}},
    {"GET", "/metrics", "health",
     "Request totals, the status-class tally and the per-route tally.",
     Auth::AdminSecret, "", {{200, "a snapshot of the counters"},
                             {401, "the admin secret is missing or wrong"}}},
    {"GET", "/openapi.json", "health", "This document.",
     Auth::None, "", {{200, "the OpenAPI 3.0 description of every route above"}}},
    {"GET", "/dashboard", "health",
     "The operator's admin page, as HTML. Not an API route; it is here because every "
     "route this server registers is in this table, with no exemptions.",
     Auth::None, "", {{200, "the page"}}},

    // ---- accounts -----------------------------------------------------------
    {"POST", "/v1/auth/register", "auth", "Create an account with an email and a password.",
     Auth::ApiKey, "{ email, password, display_name }",
     {{200, "the session and its access token"}, {400, "a field is missing or malformed"},
      {409, "that email already has an account in this project"}, kBadKey, kRateLimited}},
    {"POST", "/v1/auth/login", "auth", "Exchange an email and a password for a token.",
     Auth::ApiKey, "{ email, password }",
     {{200, "the session and its access token"}, {401, "wrong email or password"},
      kBadKey, kRateLimited}},
    {"POST", "/v1/auth/guest", "auth",
     "Sign in with no account. Send the SAME `device_id` next launch and the same guest "
     "comes back; omit it and every launch is a new player who cannot read last "
     "session's cloud save.",
     Auth::ApiKey, "{ display_name?, device_id? }",
     {{200, "the session and its access token"}, kBadKey, kRateLimited}},
    {"GET", "/v1/ping", "auth",
     "Authenticated liveness: echoes the project the api key resolved to.",
     Auth::ApiKey, "", {{200, "{ project_id }"}, kBadKey, kRateLimited}},

    // ---- leaderboards -------------------------------------------------------
    {"GET", "/v1/leaderboards/{key}/top", "leaderboards", "The top entries on a board.",
     Auth::ApiKey, "", {{200, "the ranked entries"}, {404, "no such board in this project"},
                        kBadKey, kRateLimited}},
    {"POST", "/v1/leaderboards/{key}/scores", "leaderboards",
     "Submit a score. A 'best' board keeps the better value and a 'last' board stores "
     "what it was handed — which is what a rating needs, because a rating goes down.",
     Auth::ApiKeyUser, "{ value }",
     {{200, "the stored value, the rank, and whether this call changed it"},
      {400, "the value is out of range"}, {404, "no such board"}, kBadToken, kRateLimited}},
    {"GET", "/v1/leaderboards/{key}/me", "leaderboards", "This player's own entry.",
     Auth::ApiKeyUser, "", {{200, "the value and the rank"},
                            {404, "no such board, or this player has no entry"},
                            kBadToken, kRateLimited}},
    {"POST", "/v1/leaderboards/{key}/match", "leaderboards",
     "Report a rated match. The client sends the OUTCOME, never a rating: the server "
     "does the Elo, so a ladder cannot be a number the client picked. Both players "
     "report the same `match_id` and the ratings move once.",
     Auth::ApiKeyUser, "{ opponent_id, result: win|loss|draw, match_id }",
     {{200, "both ratings, this player's rank and delta; `applied` is false for whichever "
            "player reported second"},
      {400, "a bad result, a blank match id, or reporting a match against yourself"},
      {403, "the server has no record of matching these two players"},
      {404, "no such board, or no such opponent in this project"}, kBadToken, kRateLimited}},

    // ---- cloud saves --------------------------------------------------------
    {"PUT", "/v1/saves/{slot}", "saves",
     "Write a save slot. `If-Match: <version>` makes it a compare-and-set; a refusal "
     "changes nothing, not even creating the slot.",
     Auth::ApiKeyUser, "{ data }",
     {{200, "the slot's new version and size"}, {400, "a bad slot name or an oversized payload"},
      {409, "If-Match did not match the stored version"}, kBadToken, kRateLimited}},
    {"GET", "/v1/saves/{slot}", "saves", "Read a save slot.",
     Auth::ApiKeyUser, "", {{200, "the data and its version"}, {404, "no such slot"},
                            kBadToken, kRateLimited}},
    {"DELETE", "/v1/saves/{slot}", "saves", "Delete a save slot.",
     Auth::ApiKeyUser, "", {{200, "deleted"}, {404, "no such slot"}, kBadToken, kRateLimited}},
    {"GET", "/v1/saves", "saves", "List this player's slots — metadata only, no payloads.",
     Auth::ApiKeyUser, "", {{200, "slot, version and size for each"}, kBadToken, kRateLimited}},

    // ---- inventory ----------------------------------------------------------
    {"GET", "/v1/inventory", "inventory", "Everything this player owns in this project.",
     Auth::ApiKeyUser, "", {{200, "item and quantity for each"}, kBadToken, kRateLimited}},
    {"GET", "/v1/inventory/{item}", "inventory",
     "One item's quantity. An item nobody owns answers 0, not 404.",
     Auth::ApiKeyUser, "", {{200, "{ item, qty }"}, kBadToken, kRateLimited}},
    {"POST", "/v1/inventory/{item}/grant", "inventory",
     "Add to a quantity. Send `Idempotency-Key` and a retried request replays the first "
     "result instead of granting twice.",
     Auth::ApiKeyUser, "{ amount }",
     {{200, "the resulting quantity"}, {400, "a bad item name or amount"},
      kBadToken, kRateLimited}},
    {"POST", "/v1/inventory/{item}/consume", "inventory",
     "Subtract from a quantity. Refused rather than allowed to go negative.",
     Auth::ApiKeyUser, "{ amount }",
     {{200, "the resulting quantity"}, {400, "a bad item name or amount"},
      {409, "the player does not have that many"}, kBadToken, kRateLimited}},

    // ---- the priced catalogue ----------------------------------------------
    {"GET", "/v1/store/catalog", "store", "Every offer this project sells.",
     Auth::ApiKey, "", {{200, "sku, price and reward for each"}, kBadKey, kRateLimited}},
    {"POST", "/v1/store/buy/{sku}", "store",
     "Buy an offer BY SKU. The price and the reward come from the catalogue, not from "
     "the caller — spend and grant happen in one transaction or neither happens.",
     Auth::ApiKeyUser, "{ }",
     {{200, "the item and its resulting quantity"}, {404, "no such sku"},
      {409, "the player cannot afford it"}, kBadToken, kRateLimited}},

    // ---- shared assets ------------------------------------------------------
    {"PUT", "/v1/assets/{name}", "assets",
     "Publish a project asset. Api key only: an asset belongs to the GAME, not to a "
     "player. `If-Match: <version>` makes it a compare-and-set.",
     Auth::ApiKey, "{ data, kind? }",
     {{200, "the asset's new version and size"}, {400, "a bad name or an oversized payload"},
      {409, "If-Match did not match"}, kBadKey, kRateLimited}},
    {"GET", "/v1/assets/{name}", "assets", "Fetch a project asset.",
     Auth::ApiKey, "", {{200, "the asset and its version"}, {404, "no such asset"},
                        kBadKey, kRateLimited}},
    {"GET", "/v1/assets", "assets",
     "List project assets — metadata only. `?kind=` filters.",
     Auth::ApiKey, "", {{200, "name, kind, version and size for each"}, kBadKey, kRateLimited}},
    {"DELETE", "/v1/assets/{name}", "assets", "Delete a project asset.",
     Auth::ApiKey, "", {{200, "deleted"}, {404, "no such asset"}, kBadKey, kRateLimited}},

    // ---- remote config and live events -------------------------------------
    {"GET", "/v1/config", "config",
     "Every tunable for this project. Changing one is a server-side edit, not a release.",
     Auth::ApiKey, "", {{200, "key and value for each"}, kBadKey, kRateLimited}},
    {"GET", "/v1/config/{key}", "config", "One tunable.",
     Auth::ApiKey, "", {{200, "{ key, value }"}, {404, "no such key"}, kBadKey, kRateLimited}},
    {"GET", "/v1/events", "events",
     "The live events that are running RIGHT NOW — the server decides, so a client with "
     "a wrong clock cannot start a festival early.",
     Auth::ApiKey, "", {{200, "key, name and payload for each active event"},
                        kBadKey, kRateLimited}},

    // ---- analytics ----------------------------------------------------------
    {"POST", "/v1/analytics/events", "analytics",
     "Record a gameplay event, optionally attributed to a release.",
     Auth::ApiKey, "{ name, props?, release? }",
     {{200, "recorded"}, {400, "a missing or malformed name"}, kBadKey, kRateLimited}},

    // ---- replays ------------------------------------------------------------
    {"POST", "/v1/replays", "replays", "Store a recorded run. The payload is opaque here.",
     Auth::ApiKeyUser, "{ name, data }",
     {{200, "the replay's id"}, {400, "a bad name or an oversized payload"},
      kBadToken, kRateLimited}},
    {"GET", "/v1/replays", "replays", "List this player's replays — metadata only.",
     Auth::ApiKeyUser, "", {{200, "id, name, size and time for each"}, kBadToken, kRateLimited}},
    {"GET", "/v1/replays/{id}", "replays", "Fetch one replay, payload included.",
     Auth::ApiKeyUser, "", {{200, "the replay"}, {404, "no such replay for this player"},
                            kBadToken, kRateLimited}},
    {"DELETE", "/v1/replays/{id}", "replays", "Delete one replay.",
     Auth::ApiKeyUser, "", {{200, "deleted"}, {404, "no such replay for this player"},
                            kBadToken, kRateLimited}},

    // ---- managed headless test runs -----------------------------------------
    {"POST", "/v1/testruns", "testruns",
     "Queue a headless scenario. Api key only — a test run belongs to the project.",
     Auth::ApiKey, "{ scenario, params? }",
     {{200, "the queued run"}, {400, "a missing scenario"}, kBadKey, kRateLimited}},
    {"GET", "/v1/testruns", "testruns", "Every run in this project, newest first.",
     Auth::ApiKey, "", {{200, "the runs"}, kBadKey, kRateLimited}},
    {"GET", "/v1/testruns/{id}", "testruns", "One run, with its result if it has finished.",
     Auth::ApiKey, "", {{200, "the run"}, {404, "no such run in this project"},
                        kBadKey, kRateLimited}},
    {"POST", "/v1/testruns/{id}/claim", "testruns",
     "A worker takes a pending run. Claiming one that is already running is refused, so "
     "two workers cannot both take it.",
     Auth::ApiKey, "", {{200, "the claimed run"}, {404, "no such run"},
                        {409, "already claimed"}, kBadKey, kRateLimited}},
    {"PATCH", "/v1/testruns/{id}", "testruns", "A worker reports a run finished.",
     Auth::ApiKey, "{ status: passed|failed|error, result? }",
     {{200, "recorded"}, {400, "an unknown status"}, {404, "no such run"},
      kBadKey, kRateLimited}},

    // ---- realtime -----------------------------------------------------------
    {"GET", "/v1/ws", "realtime",
     "WebSocket upgrade: lobby presence and matchmaking. The api key and the access "
     "token are checked on the upgrade request, and the `matched` event carries the "
     "side, the seed and the opponent — a seed mixed from two clients lets whoever "
     "sends second grind theirs.",
     Auth::ApiKeyUser, "", {{101, "switching protocols"},
                            {401, "the api key or the access token is missing or bad"}}},

    // ---- operator and platform administration -------------------------------
    {"POST", "/v1/admin/projects", "admin",
     "Create a project. Platform-level: this is the only route that can mint an api key.",
     Auth::AdminSecret, "{ name }",
     {{200, "the project, with its secret key returned ONCE"}, {400, "a missing name"},
      {401, "the admin secret is missing or wrong"}}},
    {"GET", "/v1/admin/projects", "admin", "Every project on this server.",
     Auth::AdminSecret, "", {{200, "the projects, public keys only"},
                             {401, "the admin secret is missing or wrong"}}},
    {"PUT", "/v1/admin/config/{key}", "admin",
     "Set a tunable and record who changed it from what to what.",
     Auth::ApiKeySecret, "{ value }",
     {{200, "the previous value, if there was one"}, {400, "a bad key or value"},
      {401, "the secret key is missing or wrong"}, kRateLimited}},
    {"DELETE", "/v1/admin/config/{key}", "admin", "Remove a tunable, audited.",
     Auth::ApiKeySecret, "", {{200, "the removed value"}, {404, "no such key"},
                              {401, "the secret key is missing or wrong"}, kRateLimited}},
    {"POST", "/v1/admin/events", "admin",
     "Define a live event with a start and an end. Seeding one INACTIVE is deliberate: "
     "a festival should be a switch an operator flips, not a permanent buff.",
     Auth::ApiKeySecret, "{ key, name, starts_at, ends_at, payload? }",
     {{200, "the event"}, {400, "a missing field or a malformed timestamp"},
      {401, "the secret key is missing or wrong"}, kRateLimited}},
    {"GET", "/v1/admin/analytics/summary", "admin",
     "Event counts per release — the shape that lets an operator compare a new release "
     "to the one it replaced before rolling it back.",
     Auth::ApiKeySecret, "", {{200, "the per-release tallies"},
                              {401, "the secret key is missing or wrong"}, kRateLimited}},
    {"GET", "/v1/admin/users", "admin", "The players in this project.",
     Auth::ApiKeySecret, "", {{200, "the users"},
                              {401, "the secret key is missing or wrong"}, kRateLimited}},
    {"POST", "/v1/admin/secret/rotate", "admin",
     "Mint a new secret key for this project. The old one stops verifying immediately "
     "and the new one is returned ONCE.",
     Auth::ApiKeySecret, "", {{200, "the new secret key"},
                              {401, "the secret key is missing or wrong"}, kRateLimited}},
    {"PUT", "/v1/admin/catalog/{sku}", "admin",
     "Define or re-price an offer. An offer must charge something and grant something.",
     Auth::ApiKeySecret, "{ currency, cost, item, amount }",
     {{200, "the offer"}, {400, "a bad sku, a zero price or a zero reward"},
      {401, "the secret key is missing or wrong"}, kRateLimited}},
    {"POST", "/v1/admin/operators", "admin",
     "Mint an operator with their OWN key and a role (viewer < admin < owner) — the "
     "thing a single shared project secret cannot express.",
     Auth::ApiKeySecret, "{ name, role }",
     {{200, "the operator, with their key returned ONCE"},
      {400, "a bad name or an unknown role"},
      {401, "the secret key is missing or wrong"}, kRateLimited}},
    {"GET", "/v1/admin/operators", "admin", "The operators of this project and their roles.",
     Auth::ApiKeySecret, "", {{200, "the operators, never their keys"},
                              {401, "the secret key is missing or wrong"}, kRateLimited}},
    };
    return ops;
}

}  // namespace web::openapi
