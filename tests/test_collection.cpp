// =============================================================================
//  tests/test_collection.cpp  —  the game list a page reads (dependency-free, CTest)
// =============================================================================
//  Two halves, checked differently:
//
//   * to_json is PURE. Field order, escaping and the trailing newline are the
//     FORMAT, not formatting — the file is committed and compared byte-for-byte,
//     so a change here is a change to an artefact.
//
//   * build_collection reads the real assets/projects. That is deliberate: it is
//     what makes the committed assets/collection.json provably what the manifests
//     bake to, and what turns "somebody added a game and forgot to re-index" into
//     a red test instead of a game missing from the page. Chapter 129 is the
//     argument — a list that is written down twice goes stale, so this one is
//     baked from the directory and held to it here.
// =============================================================================
#include "engine/project/collection.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "engine/assets.hpp"

#ifndef ASSET_ROOT
#define ASSET_ROOT "assets"
#endif

using namespace engine;

static int g_failures = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// ---- the pure half ----------------------------------------------------------

static void test_json_shape() {
    CollectionEntry e;
    e.manifest = "projects/x.gameproject";
    e.name     = "X";
    e.summary  = "one line";
    e.entry    = "fps";
    e.cover    = "textures/wall_1.hrt";
    e.readme   = "projects/x.md";
    e.package  = "deadbeef";
    e.playable = true;

    const std::string j = to_json({e});
    CHECK(contains(j, "\"manifest\": \"projects/x.gameproject\""));
    CHECK(contains(j, "\"summary\": \"one line\""));
    CHECK(contains(j, "\"playable\": true"));
    CHECK(contains(j, "\"problems\": []"));
    CHECK(!j.empty() && j.back() == '\n');          // a text artefact ends in a newline

    // Field ORDER is part of the format: a reader that diffs the committed file must
    // see a change only when the content changed.
    CHECK(j.find("\"manifest\"") < j.find("\"name\""));
    CHECK(j.find("\"name\"")     < j.find("\"summary\""));
    CHECK(j.find("\"cover\"")    < j.find("\"readme\""));
}

static void test_json_escaping() {
    CollectionEntry e;
    e.manifest = "p";
    e.name     = "say \"hi\" \\ now";       // quote and backslash
    e.summary  = std::string("a\nb\tc") + '\x01';  // newline, tab, control byte
    e.problems = {"missing asset: a\"b"};
    const std::string j = to_json({e});

    CHECK(contains(j, "say \\\"hi\\\" \\\\ now"));
    CHECK(contains(j, "a\\nb\\tc\\u0001"));
    CHECK(contains(j, "missing asset: a\\\"b"));
    // A raw control byte or a bare quote in the output is a file the page cannot parse.
    CHECK(j.find('\x01') == std::string::npos);
}

static void test_json_utf8_passthrough() {
    CollectionEntry e;
    e.manifest = "p";
    e.name     = "Nông trại";     // the project's own docs are Vietnamese
    const std::string j = to_json({e});
    CHECK(contains(j, "Nông trại"));   // UTF-8 is already valid JSON; do not mangle it
}

static void test_json_not_playable_keeps_its_problems() {
    CollectionEntry e;
    e.manifest = "projects/broken.gameproject";
    e.name     = "broken.gameproject";
    e.playable = false;
    e.problems = {"missing asset: nope.hrt", "unknown entry scene 'zzz'"};
    const std::string j = to_json({e});
    CHECK(contains(j, "\"playable\": false"));
    // Both, not the first: a card that shows one of two problems sends you round twice.
    CHECK(contains(j, "missing asset: nope.hrt"));
    CHECK(contains(j, "unknown entry scene 'zzz'"));
}

static void test_json_empty_and_multiple() {
    CHECK(to_json({}) == "{\n  \"games\": [\n  ]\n}\n");   // still valid JSON, no games

    CollectionEntry a, b;
    a.manifest = "a"; b.manifest = "b";
    const std::string j = to_json({a, b});
    CHECK(contains(j, "},\n"));            // separated
    CHECK(!contains(j, "},\n  ]"));        // and no trailing comma before the close
}

// ---- assets::list_dir, the one impure primitive this needs ------------------

static void test_list_dir() {
    assets::set_base_path(ASSET_ROOT);

    const auto all = assets::list_dir("projects");
    const auto man = assets::list_dir("projects", ".gameproject");
    CHECK(!man.empty());
    CHECK(man.size() <= all.size());
    for (const auto& n : man) CHECK(n.size() > 12 && contains(n, ".gameproject"));

    // Sorted, because the index it feeds is a committed file: two machines whose
    // readdir disagrees must not produce two different artefacts.
    for (std::size_t i = 1; i < man.size(); ++i) CHECK(man[i - 1] < man[i]);

    // A DIRECTORY whose name matches the suffix is not a file. Left in, it becomes a
    // card for a project that cannot be read — a phantom problem, reported against a
    // manifest that was never written. (This check exists because a mutation that
    // removed the is_regular_file filter survived everything else in this file.)
    {
        const std::string d = std::string(ASSET_ROOT) + "/projects/_test_dir.gameproject";
        std::filesystem::create_directories(d);
        const auto names = assets::list_dir("projects", ".gameproject");
        std::filesystem::remove_all(d);
        for (const auto& n : names) CHECK(n != "_test_dir.gameproject");
    }

    // A missing directory is an empty list, not a crash and not an error.
    CHECK(assets::list_dir("no_such_directory_here").empty());
    // A suffix longer than the name must not read off the front of it.
    CHECK(assets::list_dir("projects", "..............................").empty());
}

// ---- the impure half, against the real projects/ ---------------------------

static void test_build_collection_reads_the_directory() {
    assets::set_base_path(ASSET_ROOT);
    const std::vector<std::string> known = {"fps", "farm"};
    const auto items = build_collection("projects", known);

    CHECK(items.size() >= 2);
    for (std::size_t i = 1; i < items.size(); ++i) CHECK(items[i - 1].manifest < items[i].manifest);

    const CollectionEntry* farm = nullptr;
    for (const auto& e : items) if (contains(e.manifest, "farm")) farm = &e;
    CHECK(farm != nullptr);
    if (farm) {
        CHECK(farm->playable);                    // the repo's own games must be shippable
        CHECK(!farm->summary.empty());            // a card with no line under the name
        CHECK(!farm->cover.empty());
        CHECK(contains(farm->cover, ".hrt"));
        CHECK(!farm->package.empty());            // shippable => it has a release id
        CHECK(farm->problems.empty());
        CHECK(farm->readme == "projects/farm.md");   // picked up by convention, not a field
    }

    // Everything the directory holds is in the result — the property that makes the
    // committed index trustworthy.
    CHECK(items.size() == assets::list_dir("projects", ".gameproject").size());
}

static void test_a_broken_project_is_listed_not_hidden() {
    // A card you cannot see is a project you cannot fix. Write a deliberately broken
    // manifest, and require it to come back listed, unplayable, and explained.
    assets::set_base_path(ASSET_ROOT);
    const std::string path = std::string(ASSET_ROOT) + "/projects/_test_broken.gameproject";
    {
        std::ofstream f(path);
        f << "gameproject1\nname Broken\nschema 1\nentry farm\nasset texture nope_missing.hrt\n";
    }
    const auto items = build_collection("projects", {"fps", "farm"});
    std::filesystem::remove(path);

    const CollectionEntry* broken = nullptr;
    for (const auto& e : items) if (contains(e.manifest, "_test_broken")) broken = &e;
    CHECK(broken != nullptr);
    if (broken) {
        CHECK(!broken->playable);
        CHECK(broken->package.empty());          // never derive an id from partial content
        CHECK(!broken->problems.empty());
        CHECK(contains(broken->problems.front(), "nope_missing.hrt"));
    }
}

static void test_committed_index_is_what_the_manifests_bake_to() {
    assets::set_base_path(ASSET_ROOT);
    const auto want = to_json(build_collection("projects", {"fps", "farm"}));

    const auto got = assets::load_file("collection.json");
    CHECK(got.has_value());
    if (!got) {
        std::printf("      (re-bake it: ./build/demo --cmd collection.index projects collection.json)\n");
        return;
    }
    const std::string have(got->begin(), got->end());
    CHECK(have == want);
    if (have != want)
        std::printf("      assets/collection.json is stale — re-bake:\n"
                    "      ./build/demo --cmd collection.index projects collection.json\n");
}

int main() {
    test_json_shape();
    test_json_escaping();
    test_json_utf8_passthrough();
    test_json_not_playable_keeps_its_problems();
    test_json_empty_and_multiple();
    test_list_dir();
    test_build_collection_reads_the_directory();
    test_a_broken_project_is_listed_not_hidden();
    test_committed_index_is_what_the_manifests_bake_to();

    if (g_failures == 0) std::printf("test_collection: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
