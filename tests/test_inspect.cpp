// =============================================================================
//  tests/test_inspect.cpp  —  one project resolve, as data (CTest, real assets::)
// =============================================================================
//  inspect() replaced four hand-written copies of "read the manifest, validate it,
//  hash every declared asset". The copies had drifted, so the tests here are written
//  against the DIFFERENCES as much as the agreement: every problem is reported (not
//  just the first), a missing asset keeps its place in the list, and a project that
//  is not shippable computes no package hash at all.
// =============================================================================
#include "engine/project/inspect.hpp"

#include "engine/assets.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace engine;

static int g_failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond);  \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

static void write(const std::string& path, const std::string& text) {
    assets::write_file(path, std::vector<uint8_t>(text.begin(), text.end()));
}

static bool mentions(const std::vector<std::string>& v, const std::string& needle) {
    for (const auto& s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

int main() {
    std::filesystem::remove_all("test_inspect_tmp");
    assets::set_base_path("test_inspect_tmp");
    const std::vector<std::string> known = {"fps", "farm"};

    // ---- a manifest that is not there at all --------------------------------
    // Not "shippable with one problem" — there is no project. readable/parsed must
    // both be false so a caller can tell "broken project" from "no project".
    {
        const Inspection in = inspect("nope.gameproject", known);
        CHECK(!in.readable);
        CHECK(!in.parsed);
        CHECK(!in.shippable());
        CHECK(in.problems.size() == 1);
        CHECK(mentions(in.problems, "cannot read"));
        CHECK(in.assets.empty());
        CHECK(in.package.empty());
    }

    // ---- a file that exists but is not a manifest ---------------------------
    {
        write("junk.gameproject", "this is not a manifest\n");
        const Inspection in = inspect("junk.gameproject", known);
        CHECK(in.readable);        // we DID read it...
        CHECK(!in.parsed);         // ...it just was not one
        CHECK(mentions(in.problems, "gameproject1"));
    }

    // ---- the happy path -----------------------------------------------------
    {
        write("a.txt", "alpha");
        write("b.txt", "beta");
        write("ok.gameproject",
              "gameproject1\nname Good\nschema 1\nentry fps\n"
              "asset text a.txt\nasset text b.txt\n");
        const Inspection in = inspect("ok.gameproject", known);
        CHECK(in.readable && in.parsed && in.shippable());
        CHECK(in.problems.empty());
        CHECK(in.project.name == "Good");
        CHECK(in.project.entry == "fps");
        CHECK(in.assets.size() == 2);
        CHECK(in.assets[0].present && in.assets[0].path == "a.txt");
        CHECK(in.assets[0].bytes == 5);              // "alpha"
        CHECK(in.assets[0].hash != 0);
        CHECK(in.assets[0].hash != in.assets[1].hash);   // different bytes, different hash
        CHECK(!in.package.empty());
        CHECK(in.resources().size() == 2);

        // Inspecting twice must give the same package hash: a release id is derived
        // from this, so an unstable answer here would be an unstable release id.
        CHECK(inspect("ok.gameproject", known).package == in.package);
    }

    // ---- THE regression: every problem, not just the first ------------------
    // ops.cpp's copy returned at the first missing asset, so publishing a project
    // with three broken paths took three runs to diagnose. All three, one run.
    {
        write("three.gameproject",
              "gameproject1\nname Three\nschema 1\nentry fps\n"
              "asset text gone1.txt\nasset text gone2.txt\nasset text gone3.txt\n");
        const Inspection in = inspect("three.gameproject", known);
        CHECK(in.parsed);
        CHECK(!in.shippable());
        CHECK(in.problems.size() == 3);
        CHECK(mentions(in.problems, "gone1.txt"));
        CHECK(mentions(in.problems, "gone2.txt"));
        CHECK(mentions(in.problems, "gone3.txt"));
    }

    // ---- a missing asset keeps its place in the list ------------------------
    // The browser draws in manifest order; sorting the missing ones into a second
    // list would hide WHERE the hole is, which is half of what a hole tells you.
    {
        write("hole.gameproject",
              "gameproject1\nname Hole\nschema 1\nentry fps\n"
              "asset text a.txt\nasset text missing.txt\nasset text b.txt\n");
        const Inspection in = inspect("hole.gameproject", known);
        CHECK(in.assets.size() == 3);
        CHECK(in.assets[0].present);
        CHECK(!in.assets[1].present);
        CHECK(in.assets[1].path == "missing.txt");
        CHECK(in.assets[1].hash == 0 && in.assets[1].bytes == 0);
        CHECK(in.assets[2].present);
        // resources() drops the missing one rather than hashing a hole...
        CHECK(in.resources().size() == 2);
        // ...and precisely because the content is incomplete, NO package hash is
        // computed: a release id must never be derivable from a partial project.
        CHECK(in.package.empty());
    }

    // ---- validation errors are ordered before missing content ---------------
    // A wrong entry id explains the whole project; a missing sprite explains one
    // asset. Reading only the first line is then usually enough.
    {
        write("bad.gameproject",
              "gameproject1\nname Bad\nschema 1\nentry nosuchentry\n"
              "asset text gone.txt\n");
        const Inspection in = inspect("bad.gameproject", known);
        CHECK(in.problems.size() == 2);
        CHECK(in.problems.front().find("nosuchentry") != std::string::npos);
        CHECK(in.problems.back().find("missing asset") != std::string::npos);
    }

    // ---- content change moves the package hash ------------------------------
    {
        const std::string before = inspect("ok.gameproject", known).package;
        write("b.txt", "beta and then some");
        const std::string after = inspect("ok.gameproject", known).package;
        CHECK(!before.empty() && before != after);
    }

    // ---- the cover joins the closure (chapter 130) ---------------------------
    // A cover ships, so it is hashed like everything else that ships, and a cover that
    // is not there makes the project unshippable rather than the card empty.
    {
        write("art.hrt", "HRT1....pixels");
        write("cover_new.gameproject",
              "gameproject1\nname C\nschema 1\nentry fps\ncover art.hrt\n");
        const Inspection in = inspect("cover_new.gameproject", known);
        CHECK(in.shippable());
        CHECK(in.assets.size() == 1);
        CHECK(!in.assets.empty() && in.assets[0].type == "cover");
        CHECK(!in.assets.empty() && in.assets[0].present);
        CHECK(!in.package.empty());

        // ...and NOT twice when the manifest already declared that same path. Hashing
        // one file under two names would change the release id with no change in
        // content, which is the one thing a content-addressed id must never do.
        write("cover_dup.gameproject",
              "gameproject1\nname C\nschema 1\nentry fps\ncover art.hrt\nasset texture art.hrt\n");
        const Inspection dup = inspect("cover_dup.gameproject", known);
        CHECK(dup.shippable());
        CHECK(dup.assets.size() == 1);
        CHECK(!dup.assets.empty() && dup.assets[0].type == "texture");   // the declaration wins

        // A missing cover is a problem, named, and no package hash is derived.
        write("cover_gone.gameproject",
              "gameproject1\nname C\nschema 1\nentry fps\ncover absent.hrt\n");
        const Inspection gone = inspect("cover_gone.gameproject", known);
        CHECK(!gone.shippable());
        CHECK(mentions(gone.problems, "absent.hrt"));
        CHECK(gone.package.empty());
        CHECK(gone.assets.size() == 1);            // still LISTED, with present=false
        CHECK(!gone.assets.empty() && !gone.assets[0].present);
    }

    std::filesystem::remove_all("test_inspect_tmp");
    if (g_failures == 0) std::printf("test_inspect: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
