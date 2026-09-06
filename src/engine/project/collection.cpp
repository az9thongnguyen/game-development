// =============================================================================
//  engine/project/collection.cpp
// =============================================================================
#include "engine/project/collection.hpp"

#include <sstream>

#include "engine/assets.hpp"
#include "engine/project/inspect.hpp"

namespace engine {
namespace {

// Enough JSON escaping for the values that actually occur here — a name, a one-line
// summary, a path, a validation message — plus every control character, because a
// stray byte in a project name must not produce a file the page cannot parse.
std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 0xf];
                } else {
                    out += static_cast<char>(c);   // UTF-8 passes through unchanged
                }
        }
    }
    return out;
}

void field(std::ostringstream& o, const char* key, const std::string& v, bool last = false) {
    o << "      \"" << key << "\": \"" << esc(v) << "\"" << (last ? "\n" : ",\n");
}

} // namespace

std::string to_json(const std::vector<CollectionEntry>& items) {
    std::ostringstream o;
    o << "{\n  \"games\": [\n";
    for (std::size_t i = 0; i < items.size(); ++i) {
        const CollectionEntry& e = items[i];
        o << "    {\n";
        field(o, "manifest", e.manifest);
        field(o, "name",     e.name);
        field(o, "summary",  e.summary);
        field(o, "entry",    e.entry);
        field(o, "cover",    e.cover);
        field(o, "readme",   e.readme);
        field(o, "package",  e.package);
        o << "      \"playable\": " << (e.playable ? "true" : "false") << ",\n";
        o << "      \"problems\": [";
        for (std::size_t j = 0; j < e.problems.size(); ++j)
            o << (j ? ", " : "") << "\"" << esc(e.problems[j]) << "\"";
        o << "]\n";
        o << "    }" << (i + 1 == items.size() ? "\n" : ",\n");
    }
    o << "  ]\n}\n";
    return o.str();
}

std::vector<CollectionEntry> build_collection(const std::string& dir,
                                              const std::vector<std::string>& known_entries) {
    std::vector<CollectionEntry> out;
    for (const std::string& file : assets::list_dir(dir, ".gameproject")) {
        const std::string path = dir + "/" + file;
        const Inspection in = inspect(path, known_entries);

        CollectionEntry e;
        e.manifest = path;
        e.name     = in.project.name;
        e.summary  = in.project.summary;
        e.entry    = in.project.entry;
        e.cover    = in.project.cover;
        e.package  = in.package;
        e.playable = in.shippable();
        e.problems = in.problems;

        // A manifest too broken to parse has no name to show. Falling back to the file
        // name keeps the card identifiable — an unnamed card is one you cannot even
        // report as broken.
        if (e.name.empty()) e.name = file;

        const std::string readme = path.substr(0, path.size() - std::string(".gameproject").size()) + ".md";
        if (assets::load_file(readme)) e.readme = readme;

        out.push_back(std::move(e));
    }
    return out;
}

} // namespace engine
