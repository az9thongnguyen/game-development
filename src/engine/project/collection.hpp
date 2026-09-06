// =============================================================================
//  engine/project/collection.hpp  —  every game in one directory, as one file
// =============================================================================
//  A link to ONE game is a query string somebody has to be told. A link to the
//  collection is a page. The difference is an index: the browser cannot list a
//  directory, so something has to write down what is in it.
//
//  That "something" must not be a hand-kept list. Chapter 129 is what happens to
//  those — a suite went dark because the thing that named it went stale. So the
//  index is BAKED from the directory (assets::list_dir), the same relationship a
//  `.hrt` has to its `.recipe`/`.pix`: the committed file must be what the sources
//  bake to, and a test says so instead of the commit message.
//
//  What the page needs and `Project` does not carry: whether the game is actually
//  playable right now. A card for a game with a missing asset is not a card to hide —
//  hiding it is how you end up unable to find out what is wrong — so the entry keeps
//  its problems and the page draws them instead of a Play button.
//
//  Split on purpose: `to_json` is PURE (text in, text out, unit-testable), and
//  `build_collection` is the one impure step that reads the directory.
// =============================================================================
#pragma once

#include <string>
#include <vector>

namespace engine {

struct CollectionEntry {
    std::string manifest;   // "projects/farm.gameproject" — also the ?project= value
    std::string name;
    std::string summary;
    std::string entry;      // the scene id, for a reader who wants to know
    std::string cover;      // asset-relative .hrt, or empty
    std::string readme;     // asset-relative .md sitting next to the manifest, or empty
    std::string package;    // package hash hex; empty unless shippable
    bool        playable = false;
    std::vector<std::string> problems;   // why not, when it is not
};

// The canonical index text. Stable field order, two-space indent, LF endings — it is
// a committed artefact compared byte-for-byte, so its formatting is part of the format.
std::string to_json(const std::vector<CollectionEntry>& items);

// Inspect every `*.gameproject` directly inside `dir`, in sorted order. `known_entries`
// is passed through to validate(), so this stays free of scene knowledge like the rest
// of the spine. A README is picked up by convention — `<stem>.md` beside the manifest —
// rather than by a manifest field, because a field could name a file that is not there
// and a convention cannot.
std::vector<CollectionEntry> build_collection(const std::string& dir,
                                              const std::vector<std::string>& known_entries);

} // namespace engine
