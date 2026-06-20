// =============================================================================
//  server/leaderboard.cpp  —  leaderboard + minimal hand-written JSON
// =============================================================================
#include "server/leaderboard.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace web {
namespace {

// Scan one {"name":"...","score":N} record starting at/after `pos`. Advances `pos`
// past it. Returns false when no further "name" field is found. Tolerant of
// surrounding array/object punctuation, so it parses both a POST body and our own
// saved file.
bool next_record(const std::string& s, std::size_t& pos, std::string& name, long& value) {
    const std::size_t nk = s.find("\"name\"", pos);
    if (nk == std::string::npos) return false;
    std::size_t i = s.find(':', nk);
    if (i == std::string::npos) return false;
    ++i;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    name.clear();
    while (i < s.size() && s[i] != '"') {            // value is already sanitized on write
        if (s[i] == '\\' && i + 1 < s.size()) ++i;   // tolerate an escape, take next char
        name.push_back(s[i]);
        ++i;
    }
    if (i >= s.size()) return false;
    ++i;

    const std::size_t sk = s.find("\"score\"", i);
    if (sk == std::string::npos) return false;
    std::size_t j = s.find(':', sk);
    if (j == std::string::npos) return false;
    ++j;
    while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) ++j;
    const char* start = s.c_str() + j;
    char*       end   = nullptr;
    const long  v     = std::strtol(start, &end, 10);
    if (end == start) return false;                  // no digits

    value = v;
    pos   = static_cast<std::size_t>(end - s.c_str());
    return true;
}

} // namespace

std::string sanitize_name(const std::string& raw) {
    std::string out;
    for (char c : raw) {
        if (out.size() >= 24) break;                                  // cap length
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F) continue;                          // drop control chars
        if (c == '"' || c == '\\') continue;                          // drop JSON-breakers
        out.push_back(c);
    }
    if (out.empty()) out = "anon";
    return out;
}

void Leaderboard::add(const std::string& name, long value) {
    scores_.push_back(Score{sanitize_name(name), value});
    std::stable_sort(scores_.begin(), scores_.end(),
                     [](const Score& a, const Score& b) { return a.value > b.value; });
    if (scores_.size() > kMax) scores_.resize(kMax);
}

std::string Leaderboard::to_json() const {
    std::ostringstream o;
    o << '[';
    for (std::size_t i = 0; i < scores_.size(); ++i) {
        if (i) o << ',';
        o << "{\"name\":\"" << scores_[i].name << "\",\"score\":" << scores_[i].value << '}';
    }
    o << ']';
    return o.str();
}

bool Leaderboard::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();

    scores_.clear();
    std::size_t pos = 0;
    std::string name;
    long        value = 0;
    while (next_record(text, pos, name, value))
        scores_.push_back(Score{sanitize_name(name), value});
    std::stable_sort(scores_.begin(), scores_.end(),
                     [](const Score& a, const Score& b) { return a.value > b.value; });
    if (scores_.size() > kMax) scores_.resize(kMax);
    return true;
}

bool Leaderboard::save(const std::string& path) const {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const std::string json = to_json();
    f.write(json.data(), static_cast<std::streamsize>(json.size()));
    return static_cast<bool>(f);
}

bool parse_score_body(const std::string& body, std::string& name, long& value) {
    std::size_t pos = 0;
    return next_record(body, pos, name, value);
}

} // namespace web
