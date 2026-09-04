// =============================================================================
//  engine/project/inspect.hpp  —  what a project is made of, as data
// =============================================================================
//  "Read the manifest, validate it, hash every declared asset" is the first step of
//  launch, of publish, of the hub, and of inspect. It had been written FOUR times:
//  main.cpp's resolve_project, main.cpp's inspect branch, ops.cpp's resolve, and
//  hub_build.cpp's build_hub_view. ops.cpp:37 said in a comment "unify the three if
//  a fourth appears" — this is the fourth, so here it is.
//
//  They had already drifted, which is the point: ops.cpp's copy returned at the FIRST
//  missing asset, so `--project-publish` reported one broken file per run — fix it,
//  run again, learn about the next one — while the hub and inspect listed them all.
//  Nobody decided that; it is just what happens to four copies of one idea.
//
//  Returns DATA, never printed lines. The CLI formats it, the Studio's asset browser
//  draws it, and the hub aggregates it. Impure only in that it reads through assets::.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/project/project.hpp"
#include "engine/resource/resource.hpp"

namespace engine {

// One declared asset, resolved. A MISSING asset is still listed, with present=false:
// an asset browser that silently drops what it cannot find is the one browser you
// cannot use to find out what is wrong.
struct InspectedAsset {
    std::string type;
    std::string path;
    std::uint64_t hash  = 0;       // content hash; 0 when missing
    std::size_t   bytes = 0;       // file size;    0 when missing
    bool          present = false;
};

struct Inspection {
    std::string path;                       // the manifest path that was asked about
    bool        readable = false;           // the manifest file itself could be read
    bool        parsed   = false;           // ...and it was a gameproject1 manifest
    Project     project{};                  // default-constructed unless parsed
    std::vector<InspectedAsset> assets;     // every DECLARED asset, present or not
    std::vector<std::string>    problems;   // validation errors first, then missing assets
    std::string package;                    // package hash hex; "" unless shippable

    // Shippable = parsed, valid, and every declared asset resolves. A project that
    // cannot be read is not "shippable with one problem", it is not a project.
    [[nodiscard]] bool shippable() const { return parsed && problems.empty(); }

    // The present assets as package input. Deliberately excludes the missing ones:
    // hashing a shorter list would silently produce a DIFFERENT package hash for a
    // broken project, and a release id must never be computable from incomplete content.
    // Only meaningful when shippable(); callers publish nothing otherwise.
    [[nodiscard]] std::vector<PackagedResource> resources() const;
};

// Read + validate + hash. `known_entries` is the set of entry ids this build can
// actually launch, passed in so this stays free of scene knowledge.
Inspection inspect(const std::string& project_path,
                   const std::vector<std::string>& known_entries);

} // namespace engine
