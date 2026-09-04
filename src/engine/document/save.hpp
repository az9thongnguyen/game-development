// =============================================================================
//  engine/document/save.hpp  —  a versioned save file, with a way forward
// =============================================================================
//  Every game here will want to persist a run, and every one of them will change its
//  shape after the first player has already saved. So the format carries a VERSION and
//  the loader carries a MIGRATION CHAIN: a save written by an older build is stepped
//  forward one version at a time rather than rejected or, worse, read as if it were
//  current (which silently drops the fields the old build did not write).
//
//  Shape:
//      save 1
//      game farm
//      version 3
//      var gold 250
//      var day 4
//      section soil
//      <arbitrary lines, indented by nothing, until the next section or EOF>
//
//  `vars` are the small flat facts (gold, day, a quest flag). `sections` are opaque
//  blobs a subsystem serializes itself — the save layer does not need to understand
//  a crop grid to store one.
//
//  PURE: text in, text out. The caller moves bytes through assets::.
// =============================================================================
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace doc {

struct SaveState {
    std::string game;             // which game wrote this ("farm")
    int         version = 0;      // the game's own schema version
    std::map<std::string, std::string> vars;
    std::map<std::string, std::string> sections;   // name -> raw text (may be multi-line)

    // Convenience, because every caller would otherwise write the same find/default.
    [[nodiscard]] std::string var(const std::string& key, const std::string& fallback = {}) const;
    [[nodiscard]] long long   num(const std::string& key, long long fallback = 0) const;
    void set(const std::string& key, const std::string& value);
    void set(const std::string& key, long long value);
};

std::string             to_text(const SaveState& s);
std::optional<SaveState> parse_save(const std::string& text);

// One step forward. A game registers a chain of these; `load` runs them in order
// until the state reaches `target`.
struct Migration {
    int from = 0;                                // applies to a save at this version
    std::function<void(SaveState&)> step;        // leaves it at from + 1
};

// Read a save and bring it up to `target_version`.
//   - a save from a NEWER build is refused: a loader that ignores fields it does not
//     understand will write the file back without them, which is data loss disguised
//     as compatibility.
//   - a gap in the chain is refused rather than skipped.
// Returns nullopt with `why` filled in.
std::optional<SaveState> load_save(const std::string& text, const std::string& expect_game,
                                   int target_version, const std::vector<Migration>& chain,
                                   std::string* why = nullptr);

} // namespace doc
