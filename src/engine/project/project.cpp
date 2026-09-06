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

        // Free text to the end of the line: a name and a one-line summary are prose,
        // and prose has spaces in it.
        auto rest_of_line = [&ls] {
            std::string rest;
            std::getline(ls, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            return rest;
        };

        if (key == "name") {
            p.name = rest_of_line();
        } else if (key == "summary") {
            p.summary = rest_of_line();
        } else if (key == "cover") {
            ls >> p.cover;
        } else if (key == "schema") {
            int v;
            if (!(ls >> v)) return std::nullopt;  // malformed schema value → unusable
            p.schema = v;
        } else if (key == "entry") {
            ls >> p.entry;
        } else if (key == "asset") {
            AssetRef a;
            if (ls >> a.type >> a.path) p.assets.push_back(a);  // well-formed decls only
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
    // Only when set. An empty `summary` line would parse back to the same Project, but
    // it would REWRITE every manifest that has never heard of summaries the first time
    // anything saved one — and a diff nobody asked for is how a round-trip test stops
    // being evidence.
    if (!p.summary.empty()) out << "summary " << p.summary << "\n";
    if (!p.cover.empty())   out << "cover " << p.cover << "\n";
    for (const auto& a : p.assets)
        out << "asset " << a.type << " " << a.path << "\n";
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

    // The collection page decodes `.hrt` by hand — it is the one raster format this
    // project has. A cover named `.png` is not a slow path, it is a blank card.
    if (!p.cover.empty() && !(p.cover.size() > 4 &&
                              p.cover.compare(p.cover.size() - 4, 4, ".hrt") == 0)) {
        errs.push_back("cover must be a .hrt file (got '" + p.cover + "')");
    }

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
