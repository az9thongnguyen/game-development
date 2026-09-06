// =============================================================================
//  engine/asset/provenance.hpp  —  where every picture in this repo came from
// =============================================================================
//  CLAUDE.md carries a rule with no enforcement: every new `.hrt` "must gain a line
//  in assets/ATTRIBUTION.md in the same change". A rule a human has to remember is a
//  rule that will be forgotten, and this one already had been — when this file was
//  written the repository held 23 `.hrt` files and ATTRIBUTION.md named three. The
//  other twenty were covered by a closing paragraph saying everything else was made
//  by code here. That sentence is probably true. It is not CHECKABLE, which is the
//  same as not being evidence.
//
//  So: don't ask a person to keep a list. Derive it. The three offline doors leave
//  different marks on disk, and the marks are enough to tell them apart:
//
//    `asset.import`   a `.pack` names the import, src -> dst   -> Imported
//    `asset.texture`  a sibling `<stem>.recipe`                -> Generated
//    `asset.pixels`   a sibling `<stem>.pix`                   -> Drawn
//
//  Anything else is `Unrecorded`, and that is the whole point: a `.hrt` that appears
//  with no source and no declaration is a licence question nobody can answer, and
//  `Ledger::ok()` goes false the moment one exists. Art with no surviving source
//  (this repo has twenty such files, generated years of chapters ago) is `Declared` —
//  named explicitly in a `.pack`, which is a weaker claim than a re-runnable bake and
//  says so.
//
//  Pure: parsing, deciding and rendering take data and return data. Only
//  `scan_provenance` touches `assets::`.
// =============================================================================
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace engine {

// How a `.hrt` got here. Ordered by how strong the claim is: an Imported or Generated
// or Drawn file can be re-baked from a committed source and byte-compared; a Declared
// one is a promise; an Unrecorded one is a hole.
enum class Origin { Imported, Generated, Drawn, Declared, Unrecorded };

const char* origin_name(Origin o);

// One asset pack: a licence, and the files it accounts for.
//
// It covers both directions on purpose. `import src dst` is foreign art that came
// through `asset.import` and can be re-run; `file path` is a file this pack simply
// vouches for. One format because the QUESTION is one question — who is answerable
// for this file — and splitting it by answer would let a file belong to neither.
struct Pack {
    std::string name;         // free text to the end of the line
    std::string author;
    std::string source;       // where it came from (URL, or this repository)
    std::string licence;      // short id, e.g. CC0-1.0
    std::string licence_url;
    std::string added;        // ISO date
    std::string note;         // one line of prose, free text

    std::vector<std::pair<std::string, std::string>> imports;  // src -> dst
    std::vector<std::string>                         files;    // declared, no source

    // Where this pack was read from. Set by scan_provenance, never parsed and never
    // written: a file does not carry its own name, and round-tripping one that did
    // would rewrite every pack the first time anything saved one.
    std::string path;
};

// `assetpack1` magic, then `key value` lines. Unknown keys are ignored, as in a
// manifest: additive fields must not make an old reader fail closed.
std::optional<Pack> parse_pack(const std::string& text);
std::string         to_text(const Pack& p);

// What one file's line in the ledger says.
struct Provenance {
    std::string path;      // base-relative `.hrt`
    Origin      origin = Origin::Unrecorded;
    // What answers for it: the file it bakes from, or — for a Declared asset, which
    // bakes from nothing — the `.pack` that vouches. Empty only when Unrecorded.
    std::string source;
    std::string licence;   // "" means the repository's own licence
    std::string pack;      // the pack that claims it ("" for a sibling source)
};

struct Ledger {
    std::vector<Provenance>  files;
    std::vector<std::string> problems;

    // Every file accounted for and nothing contradictory. This is the boolean the
    // rule in CLAUDE.md always implied and never had.
    [[nodiscard]] bool ok() const;
    [[nodiscard]] int  unrecorded() const;
};

// PURE. `hrt` is every `.hrt` under the asset root; `sources` is every `.recipe` and
// `.pix`; `packs` is every parsed `.pack`. All base-relative.
//
// A file matched by more than one rule is a PROBLEM, not a precedence puzzle: two
// answers to "who is answerable for this" means the answer is unknown.
Ledger attribute(const std::vector<std::string>& hrt,
                 const std::vector<std::string>& sources,
                 const std::vector<Pack>&        packs);

// PURE. The generated table, without the markers around it.
std::string ledger_markdown(const Ledger& l);

// The markers the generated region lives between in ATTRIBUTION.md. Prose above and
// below is hand-written and survives every bake — the machine owns the ledger, a
// person owns the explanation, and neither overwrites the other.
extern const char* const kLedgerBegin;
extern const char* const kLedgerEnd;

// PURE. Returns the document with the region between the markers replaced. Fails
// (nullopt) when a marker is missing or they are out of order — an absent marker
// means someone edited the file by hand, and appending would silently produce two
// ledgers.
std::optional<std::string> splice_ledger(const std::string& doc, const std::string& body);

// I/O: walk the asset root, read every `.pack`, and decide.
Ledger scan_provenance();

} // namespace engine
