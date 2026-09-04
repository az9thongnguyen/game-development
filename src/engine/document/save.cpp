// =============================================================================
//  engine/document/save.cpp
// =============================================================================
#include "engine/document/save.hpp"

#include <sstream>

namespace doc {

namespace {
constexpr const char* kMagic = "save";
constexpr int         kFileFormat = 1;
}  // namespace

std::string SaveState::var(const std::string& key, const std::string& fallback) const {
    const auto it = vars.find(key);
    return it == vars.end() ? fallback : it->second;
}

long long SaveState::num(const std::string& key, long long fallback) const {
    const auto it = vars.find(key);
    if (it == vars.end()) return fallback;
    try { return std::stoll(it->second); } catch (...) { return fallback; }
}

void SaveState::set(const std::string& key, const std::string& value) { vars[key] = value; }
void SaveState::set(const std::string& key, long long value) { vars[key] = std::to_string(value); }

std::string to_text(const SaveState& s) {
    std::string out = std::string(kMagic) + " " + std::to_string(kFileFormat) + "\n";
    out += "game " + s.game + "\n";
    out += "version " + std::to_string(s.version) + "\n";
    // std::map iterates in key order, so the same state always writes the same bytes.
    // A save that reorders itself between writes makes every diff and every content
    // hash useless.
    for (const auto& [k, v] : s.vars) out += "var " + k + " " + v + "\n";
    for (const auto& [name, body] : s.sections) {
        out += "section " + name + "\n";
        out += body;
        if (!body.empty() && body.back() != '\n') out += "\n";
        out += "endsection\n";
    }
    return out;
}

std::optional<SaveState> parse_save(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) return std::nullopt;
    {
        std::istringstream head(line);
        std::string magic; int format = 0;
        if (!(head >> magic >> format) || magic != kMagic) return std::nullopt;
        if (format > kFileFormat) return std::nullopt;   // a newer container, not ours
    }

    SaveState s;
    while (std::getline(in, line)) {
        std::istringstream ln(line);
        std::string tok;
        if (!(ln >> tok)) continue;
        if (tok == "game") {
            ln >> s.game;
        } else if (tok == "version") {
            if (!(ln >> s.version)) return std::nullopt;
        } else if (tok == "var") {
            std::string key, value;
            if (!(ln >> key)) return std::nullopt;
            std::getline(ln, value);
            if (!value.empty() && value.front() == ' ') value.erase(0, 1);
            s.vars[key] = value;
        } else if (tok == "section") {
            std::string name;
            if (!(ln >> name)) return std::nullopt;
            std::string body;
            while (std::getline(in, line) && line != "endsection") body += line + "\n";
            s.sections[name] = body;
        }
        // Unknown keys are ignored so an older build can still READ a newer file's
        // shape — but load_save still refuses to USE one, because reading it here and
        // writing it back is how the unknown fields get dropped.
    }
    return s;
}

std::optional<SaveState> load_save(const std::string& text, const std::string& expect_game,
                                   int target_version, const std::vector<Migration>& chain,
                                   std::string* why) {
    const auto fail = [why](const std::string& msg) -> std::optional<SaveState> {
        if (why) *why = msg;
        return std::nullopt;
    };
    auto s = parse_save(text);
    if (!s) return fail("not a save file this build understands");
    if (!expect_game.empty() && s->game != expect_game)
        return fail("this save belongs to '" + s->game + "', not '" + expect_game + "'");
    if (s->version > target_version)
        return fail("this save was written by a newer version (" +
                    std::to_string(s->version) + " > " + std::to_string(target_version) + ")");

    while (s->version < target_version) {
        const Migration* step = nullptr;
        for (const Migration& m : chain)
            if (m.from == s->version && m.step) step = &m;
        if (step == nullptr)
            return fail("no migration from save version " + std::to_string(s->version));
        step->step(*s);
        // A migration that forgets to bump the version would otherwise loop forever;
        // doing it here means a migration only has to describe the data change.
        s->version += 1;
    }
    if (why) why->clear();
    return s;
}

} // namespace doc
