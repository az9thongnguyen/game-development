// =============================================================================
//  engine/asset/provenance.cpp
// =============================================================================
#include "engine/asset/provenance.hpp"

#include <algorithm>
#include <sstream>

#include "engine/assets.hpp"

namespace engine {

const char* const kLedgerBegin = "<!-- BEGIN LEDGER (generated) -->";
const char* const kLedgerEnd   = "<!-- END LEDGER (generated) -->";

const char* origin_name(Origin o) {
    switch (o) {
        case Origin::Imported:  return "imported";
        case Origin::Generated: return "generated";
        case Origin::Drawn:     return "drawn";
        case Origin::Mixed:     return "mixed";
        case Origin::Declared:  return "declared";
        case Origin::Unrecorded:return "UNRECORDED";
    }
    return "UNRECORDED";  // unreachable; a new enumerator must not read as accounted-for
}

bool Ledger::ok() const { return problems.empty() && unrecorded() == 0; }

int Ledger::unrecorded() const {
    int n = 0;
    for (const auto& f : files)
        if (f.origin == Origin::Unrecorded) ++n;
    return n;
}

// ---------------------------------------------------------------------------- parse

std::optional<Pack> parse_pack(const std::string& text) {
    std::istringstream in(text);
    std::string        line;
    bool               seen_magic = false;
    Pack               p;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ls(line);
        std::string        key;
        if (!(ls >> key)) continue;

        if (!seen_magic) {
            if (key != "assetpack1") return std::nullopt;
            seen_magic = true;
            continue;
        }

        auto rest = [&ls] {
            std::string r;
            std::getline(ls, r);
            if (!r.empty() && r.front() == ' ') r.erase(r.begin());
            return r;
        };

        if (key == "name")             p.name = rest();
        else if (key == "author")      p.author = rest();
        else if (key == "source")      p.source = rest();
        else if (key == "licence")     p.licence = rest();
        else if (key == "licence_url") p.licence_url = rest();
        else if (key == "added")       p.added = rest();
        else if (key == "note")        p.note = rest();
        else if (key == "import") {
            std::string src, dst;
            if (ls >> src >> dst) p.imports.emplace_back(src, dst);  // well-formed only
        } else if (key == "file") {
            std::string path;
            if (ls >> path) p.files.push_back(path);
        }
        // unknown keys ignored: additive fields must not make an old reader fail
    }

    if (!seen_magic) return std::nullopt;
    return p;
}

std::string to_text(const Pack& p) {
    std::ostringstream out;
    out << "assetpack1\n";
    auto field = [&out](const char* k, const std::string& v) {
        if (!v.empty()) out << k << " " << v << "\n";
    };
    field("name", p.name);
    field("author", p.author);
    field("source", p.source);
    field("licence", p.licence);
    field("licence_url", p.licence_url);
    field("added", p.added);
    field("note", p.note);
    for (const auto& i : p.imports) out << "import " << i.first << " " << i.second << "\n";
    for (const auto& f : p.files)   out << "file " << f << "\n";
    return out.str();
}

// ---------------------------------------------------------------------------- decide

namespace {

// `textures/town.hrt` + ".pix" -> `textures/town.pix`. The sibling convention IS the
// mark `asset.texture` and `asset.pixels` leave: they write `<stem>.hrt` next to the
// source they were given, and every call in this repo does exactly that.
std::string sibling(const std::string& hrt, const char* ext) {
    const auto dot = hrt.rfind('.');
    return (dot == std::string::npos ? hrt : hrt.substr(0, dot)) + ext;
}

bool has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

Ledger attribute(const std::vector<std::string>& hrt,
                 const std::vector<std::string>& sources,
                 const std::vector<Pack>&        packs) {
    Ledger l;

    // A claim is one pack saying it is answerable for one path.
    struct Claim { std::size_t pack; bool imported; std::string src; };
    std::vector<std::pair<std::string, Claim>> claims;
    for (std::size_t i = 0; i < packs.size(); ++i) {
        for (const auto& imp : packs[i].imports)
            claims.push_back({imp.second, {i, true, imp.first}});
        for (const auto& f : packs[i].files)
            claims.push_back({f, {i, false, ""}});
    }

    // A pack that names a file which is not there is a stale claim. Left silent it
    // reads as coverage — the ledger would say "all accounted for" about a file that
    // no longer exists, which is the failure this whole file is against.
    for (const auto& c : claims)
        if (!has(hrt, c.first))
            l.problems.push_back("pack '" + packs[c.second.pack].name + "' claims '" +
                                 c.first + "', which is not an asset here");

    for (const auto& path : hrt) {
        Provenance pv;
        pv.path = path;

        std::vector<const Claim*> mine;
        for (const auto& c : claims)
            if (c.first == path) mine.push_back(&c.second);

        const std::string pix    = sibling(path, ".pix");
        const std::string recipe = sibling(path, ".recipe");
        const std::string mixsrc = sibling(path, ".mix");
        const bool        drawn  = has(sources, pix);
        const bool        gen    = has(sources, recipe);
        const bool        mixed  = has(sources, mixsrc);

        const int answers = static_cast<int>(mine.size()) + (drawn ? 1 : 0) + (gen ? 1 : 0) +
                            (mixed ? 1 : 0);
        if (answers > 1) {
            // Two answers to "who is answerable for this" is not a precedence puzzle
            // to resolve quietly; it means the answer is unknown.
            l.problems.push_back("'" + path + "' has " + std::to_string(answers) +
                                 " origins claimed; exactly one is an answer");
        }

        if (!mine.empty()) {
            const Claim& c = *mine.front();
            pv.origin  = c.imported ? Origin::Imported : Origin::Declared;
            // A declared asset bakes from nothing, so the honest answer to "what
            // answers for this" is the pack itself — which is also the file to open
            // when you want to know who said so.
            pv.source  = c.imported ? c.src : packs[c.pack].path;
            pv.licence = packs[c.pack].licence;
            pv.pack    = packs[c.pack].name;
        } else if (drawn) {
            pv.origin = Origin::Drawn;
            pv.source = pix;
        } else if (gen) {
            pv.origin = Origin::Generated;
            pv.source = recipe;
        } else if (mixed) {
            pv.origin = Origin::Mixed;
            pv.source = mixsrc;
        }
        l.files.push_back(std::move(pv));
    }

    return l;
}

// ---------------------------------------------------------------------------- render

namespace {

// A cell in a pipe table: `|` would end the column early, so it is escaped rather
// than dropped. Nothing here is user input today, and the table is byte-compared by a
// test, so a path with a pipe in it must change the output rather than corrupt it.
std::string cell(const std::string& s) {
    if (s.empty()) return "—";
    std::string out;
    for (char c : s) {
        if (c == '|') out += "\\|";
        else          out += c;
    }
    return out;
}

} // namespace

std::string ledger_markdown(const Ledger& l) {
    std::ostringstream out;
    out << "| Asset | Origin | Source | Licence |\n";
    out << "|---|---|---|---|\n";
    for (const auto& f : l.files) {
        out << "| `" << cell(f.path) << "` | " << origin_name(f.origin) << " | ";
        if (f.source.empty()) out << "—";
        else                  out << "`" << cell(f.source) << "`";
        out << " | " << (f.licence.empty() ? std::string("this repository")
                                           : cell(f.licence))
            << " |\n";
    }
    out << "\n" << l.files.size() << " raster assets, "
        << l.unrecorded() << " unrecorded.\n";
    for (const auto& p : l.problems) out << "\n> **problem:** " << p << "\n";
    return out.str();
}

std::optional<std::string> splice_ledger(const std::string& doc, const std::string& body) {
    const auto b = doc.find(kLedgerBegin);
    if (b == std::string::npos) return std::nullopt;
    const auto e = doc.find(kLedgerEnd, b);
    if (e == std::string::npos) return std::nullopt;

    std::string out = doc.substr(0, b);
    out += kLedgerBegin;
    out += "\n\n";
    out += body;
    out += "\n";
    out += doc.substr(e);
    return out;
}

// ---------------------------------------------------------------------------- scan

Ledger scan_provenance() {
    // The whole tree, not a list of folders: `pieces/` and `sprites/` hold `.hrt`
    // files and neither is `textures/`. A ledger that scanned an enumerated list
    // would be complete only until someone made a folder.
    const auto hrt   = assets::list_tree("", ".hrt");
    auto       srcs  = assets::list_tree("", ".pix");
    const auto recs  = assets::list_tree("", ".recipe");
    const auto mixes = assets::list_tree("", ".mix");
    srcs.insert(srcs.end(), recs.begin(), recs.end());
    srcs.insert(srcs.end(), mixes.begin(), mixes.end());

    Ledger bad;
    std::vector<Pack> packs;
    for (const auto& p : assets::list_tree("", ".pack")) {
        auto bytes = assets::load_file(p);
        if (!bytes) { bad.problems.push_back("cannot read '" + p + "'"); continue; }
        auto pack = parse_pack(std::string(bytes->begin(), bytes->end()));
        if (!pack) { bad.problems.push_back("'" + p + "' is not an assetpack1 file"); continue; }
        if (pack->name.empty()) pack->name = p;   // a nameless pack still has a path
        pack->path = p;
        packs.push_back(*pack);
    }

    Ledger l = attribute(hrt, srcs, packs);
    l.problems.insert(l.problems.begin(), bad.problems.begin(), bad.problems.end());
    return l;
}

} // namespace engine
