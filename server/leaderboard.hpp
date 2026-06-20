// =============================================================================
//  server/leaderboard.hpp  —  a tiny persisted high-score list (the "online bit")
// =============================================================================
//  Demonstrates the game↔service boundary from requirements.md §11: the WASM game
//  could POST a score and GET the table over HTTP. Hand-written JSON (in the spirit
//  of the engine's .hrt loader and farm serializer) — no JSON library.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace web {

struct Score {
    std::string name;
    long        value = 0;
};

class Leaderboard {
public:
    // Insert a score (name is sanitized), keep sorted highest-first, cap the size.
    void add(const std::string& name, long value);

    const std::vector<Score>& scores() const { return scores_; }

    std::string to_json() const;                  // [{"name":"..","score":N},…]
    bool        load(const std::string& path);    // read scores.json (best-effort)
    bool        save(const std::string& path) const;

    static constexpr std::size_t kMax = 100;

private:
    std::vector<Score> scores_;
};

// Clean a user-supplied name: cap length, drop control chars and the JSON-breaking
// characters (" and \). Keeps the emitted JSON well-formed and uninjectable.
std::string sanitize_name(const std::string& raw);

// Minimal extractor for {"name":"...","score":N} (used for the POST body and load).
// Returns false if either field is missing.
bool parse_score_body(const std::string& body, std::string& name, long& value);

} // namespace web
