// =============================================================================
//  engine/project/project.cpp  —  game.project manifest parse/validate/emit
// =============================================================================
#include "engine/project/project.hpp"

#include <sstream>

namespace engine {

std::optional<Project> parse_project(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    bool seen_magic = false;
    Project p;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();  // tolerate CRLF
        std::istringstream ls(line);
        std::string key;
        if (!(ls >> key)) continue;  // blank / whitespace-only line

        if (!seen_magic) {
            if (key != "gameproject1") return std::nullopt;  // fail closed on bad magic
            seen_magic = true;
            continue;
        }

        if (key == "name") {
            std::string rest;
            std::getline(ls, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            p.name = rest;
        } else if (key == "schema") {
            int v;
            if (!(ls >> v)) return std::nullopt;  // malformed schema value → unusable
            p.schema = v;
        } else if (key == "entry") {
            ls >> p.entry;
        }
        // unknown keys: ignored (forward-compatible additive fields)
    }

    if (!seen_magic) return std::nullopt;
    return p;
}

std::string to_text(const Project& p) {
    std::ostringstream out;
    out << "gameproject1\n"
        << "name " << p.name << "\n"
        << "schema " << p.schema << "\n"
        << "entry " << p.entry << "\n";
    return out.str();
}

std::vector<std::string> validate(const Project& p,
                                  const std::vector<std::string>& known_entries) {
    std::vector<std::string> errs;

    if (p.schema < 1 || p.schema > kProjectSchema) {
        errs.push_back("unsupported schema version " + std::to_string(p.schema) +
                       " (this build supports up to " + std::to_string(kProjectSchema) + ")");
    }
    if (p.name.empty()) errs.push_back("project name is required");

    if (p.entry.empty()) {
        errs.push_back("entry scene is required");
    } else {
        bool known = false;
        for (const auto& e : known_entries)
            if (e == p.entry) { known = true; break; }
        if (!known) {
            std::string list;
            for (size_t i = 0; i < known_entries.size(); ++i) {
                if (i) list += ", ";
                list += known_entries[i];
            }
            errs.push_back("unknown entry scene '" + p.entry + "' (known: " + list + ")");
        }
    }
    return errs;
}

} // namespace engine
