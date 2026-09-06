// =============================================================================
//  tests/test_commands.cpp  —  the command registry and the platform-spine bindings
// =============================================================================
//  The registry exists so a GUI action and a CLI verb cannot be two implementations
//  that merely agree today. These tests pin the properties that make that true:
//  every registered command has a working handler, an unknown id fails loudly rather
//  than silently, re-registering replaces rather than duplicates, and the release
//  commands reach the same engine::release ops the CLI calls.
// =============================================================================
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "engine/asset/provenance.hpp"
#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/commands/asset_commands.hpp"
#include "engine/commands/release_commands.hpp"
#include "engine/image.hpp"
#include "engine/paint/pixel_source.hpp"

#ifndef ASSET_ROOT
#define ASSET_ROOT "."
#endif

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

static void test_registry() {
    cmd::clear();
    CHECK(cmd::all().empty());

    int calls = 0;
    cmd::register_command(cmd::Info{"test.one", "One", "", ""},
                          [&calls](const std::vector<std::string>&) {
                              ++calls;
                              return engine::OpResult{true, "did one"};
                          });
    CHECK(cmd::exists("test.one"));
    CHECK(cmd::all().size() == 1);

    const engine::OpResult r = cmd::run("test.one");
    CHECK(r.ok);
    CHECK(r.message == "did one");
    CHECK(calls == 1);

    // An unknown id fails with a message naming it. Silence here means a mistyped
    // command on a command line looks like success.
    const engine::OpResult bad = cmd::run("test.nope");
    CHECK(!bad.ok);
    CHECK(bad.message.find("test.nope") != std::string::npos);

    // Arguments reach the handler unchanged.
    std::vector<std::string> seen;
    cmd::register_command(cmd::Info{"test.args", "Args", "", ""},
                          [&seen](const std::vector<std::string>& a) {
                              seen = a;
                              return engine::OpResult{true, ""};
                          });
    cmd::run("test.args", {"a", "b c", ""});
    CHECK(seen.size() == 3);
    CHECK(seen[1] == "b c");     // an argument with a space stays one argument
    CHECK(seen[2].empty());

    // Re-registering REPLACES. Two handlers for one id would surface as "sometimes
    // the wrong thing happens", which is worse than the last one winning.
    cmd::register_command(cmd::Info{"test.one", "One again", "", ""},
                          [](const std::vector<std::string>&) {
                              return engine::OpResult{true, "replaced"};
                          });
    CHECK(cmd::all().size() == 2);
    CHECK(cmd::run("test.one").message == "replaced");
    CHECK(calls == 1);           // the original handler is gone, not merely shadowed

    // Registration order is stable, so a palette does not reshuffle between runs.
    CHECK(cmd::all()[0].id == "test.one");
    CHECK(cmd::all()[1].id == "test.args");

    // Junk registrations are refused rather than stored as landmines.
    cmd::register_command(cmd::Info{"", "No id", "", ""},
                          [](const std::vector<std::string>&) { return engine::OpResult{true, ""}; });
    cmd::register_command(cmd::Info{"test.null", "No handler", "", ""}, nullptr);
    CHECK(cmd::all().size() == 2);
    CHECK(!cmd::exists("test.null"));
    CHECK(!cmd::run("test.null").ok);
}

// The palette's two moving parts: narrowing a list by typing, and taking a command
// back out again when the object it was bound to goes away.
static void test_filter_and_unregister() {
    cmd::clear();
    const auto noop = [](const std::vector<std::string>&) { return engine::OpResult{true, ""}; };
    cmd::register_command(cmd::Info{"release.promote", "Promote a release", "", ""}, noop);
    cmd::register_command(cmd::Info{"release.rollback", "Roll back a channel", "", ""}, noop);
    cmd::register_command(cmd::Info{"map.save", "Save the map", "Cmd+S", ""}, noop);

    CHECK(cmd::filter("").size() == 3);          // no query = everything
    CHECK(cmd::filter("map").size() == 1);
    CHECK(cmd::filter("MAP").size() == 1);       // case-insensitive: nobody types the dot-case

    // A subsequence, not a substring — that is what makes a few letters enough.
    CHECK(cmd::filter("rp").size() == 1);
    CHECK(cmd::filter("rp")[0] == 0);            // release.promote
    CHECK(cmd::filter("rlb").size() == 1);
    CHECK(cmd::filter("zz").empty());

    // The title is searchable too, so "roll" finds the command whose id says "rollback"
    // and whose title is the words a person would actually reach for.
    CHECK(cmd::filter("Roll back").size() == 1);

    // Results stay in registration order rather than being ranked, so the same
    // keystrokes always select the same command however the registry grows.
    const auto both = cmd::filter("release");
    CHECK(both.size() == 2);
    CHECK(both[0] == 0 && both[1] == 1);

    // A window that registered handlers capturing itself must be able to take them
    // back out; otherwise the palette keeps a dangling call.
    CHECK(cmd::unregister("map.save"));
    CHECK(!cmd::exists("map.save"));
    CHECK(cmd::all().size() == 2);
    CHECK(!cmd::unregister("map.save"));         // twice is false, not a crash
    CHECK(cmd::filter("").size() == 2);
    // ...and the surviving entries keep their handlers, not just their names.
    CHECK(cmd::run("release.promote").ok);
}

static void test_release_commands() {
    cmd::clear();
    cmd::register_release_commands({"fps"});

    // EVERY registered command must actually run. A registry whose entries are
    // listed by a palette but crash when invoked is worse than no palette.
    for (const auto& i : cmd::all()) {
        CHECK(!i.id.empty());
        CHECK(!i.title.empty());
        const engine::OpResult r = cmd::run(i.id, {});
        // With no arguments a mutating command must refuse with its usage line
        // rather than run with empty inputs.
        if (!r.ok) CHECK(r.message.rfind("usage:", 0) == 0);
    }

    CHECK(cmd::exists("project.publish"));
    CHECK(cmd::exists("release.promote"));
    CHECK(cmd::exists("release.rollback"));
    CHECK(cmd::exists("release.status"));
    CHECK(cmd::exists("release.log"));

    // Too few arguments is a usage error, not a publish with empty inputs.
    const engine::OpResult few = cmd::run("project.publish", {"a", "b"});
    CHECK(!few.ok);
    CHECK(few.message.find("usage:") != std::string::npos);

    // An EMPTY argument is refused too, and the reason is why this matters: the CLI
    // used to default a missing reason to "", which wrote audit entries that explain
    // nothing. A blank line in an audit log is worse than no line — it looks like
    // evidence. The Studio already refuses; these make the two agree.
    for (const auto& args : std::vector<std::vector<std::string>>{
             {"", "development", "why"},
             {"proj", "", "why"},
             {"proj", "development", ""}}) {
        const engine::OpResult r = cmd::run("project.publish", args);
        CHECK(!r.ok);
        CHECK(r.message.find("usage:") != std::string::npos);
    }
    CHECK(!cmd::run("release.promote", {"development", "preview", ""}).ok);
    CHECK(!cmd::run("release.rollback", {"production", "abc", ""}).ok);

    // Read-only commands take no required arguments, so an empty call is fine.
    CHECK(cmd::run("release.log", {}).ok);

    // Reads are safe to run for real against the repo's own store.
    assets::set_base_path(ASSET_ROOT "/assets");
    const engine::OpResult st = cmd::run("release.status");
    CHECK(st.ok);
    CHECK(st.message.find("development") != std::string::npos);
    CHECK(st.message.find("production") != std::string::npos);

    const engine::OpResult lg = cmd::run("release.log", {"production"});
    CHECK(lg.ok);
    // Either there is history mentioning production, or the explicit empty message —
    // both are correct; a silent empty string is not.
    CHECK(!lg.message.empty());

    // A promotion between channels that do not exist fails cleanly rather than
    // creating them.
    const engine::OpResult nope = cmd::run("release.promote", {"nosuch", "alsonot", "why"});
    CHECK(!nope.ok);
    CHECK(!nope.message.empty());
}

// project.inspect is a READ command, and --project-inspect is an alias onto it rather
// than a second formatter (D16). Tested here because the day it becomes two code paths
// again, it will be by someone printing "just this one extra thing" in main.cpp.
void test_inspect_command() {
    cmd::clear();
    cmd::register_release_commands({"fps"});
    CHECK(cmd::exists("project.inspect"));

    // A read of something that is not there is a failed OpResult with a reason, not
    // a crash and not a cheerful empty report.
    const engine::OpResult gone = cmd::run("project.inspect", {"no/such.gameproject"});
    CHECK(!gone.ok);
    CHECK(gone.message.find("cannot read") != std::string::npos);

    // A blank argument is refused like every other command's.
    CHECK(!cmd::run("project.inspect", {""}).ok);
    CHECK(!cmd::run("project.inspect", {}).ok);

    // ...and the real project reports OK, with one line per declared asset.
    assets::set_base_path(ASSET_ROOT "/assets");
    const engine::OpResult ok = cmd::run("project.inspect", {"projects/creator.gameproject"});
    CHECK(ok.ok);
    CHECK(ok.message.find("status OK") != std::string::npos);
    CHECK(ok.message.find("maps/level_00.map") != std::string::npos);
}


// -----------------------------------------------------------------------------
//  The three doors into `.hrt`. `asset.import` brings a picture in from a format we
//  did not invent; `asset.texture` bakes one this project GENERATED; `asset.pixels`
//  bakes one a person DREW. They matter as a set: a tile from the CC0 pack, a tile
//  from the Texture Lab and a tile typed out by hand are the same kind of file by the
//  time anything downstream sees them, and that is the only reason "support both art
//  sources" costs the engine nothing.
// -----------------------------------------------------------------------------
static void test_asset_commands() {
    assets::set_base_path(ASSET_ROOT "/assets");
    cmd::clear();
    cmd::register_asset_commands();
    CHECK(cmd::all().size() == 5);   // three doors, the ledger, and the way in

    // D17: writing commands refuse blank arguments. A destination nobody named is not
    // a default, it is a missing decision — and here it would overwrite something.
    CHECK(!cmd::run("asset.texture", {}).ok);
    CHECK(!cmd::run("asset.texture", {"a.recipe"}).ok);
    CHECK(!cmd::run("asset.texture", {"", "b.hrt"}).ok);
    CHECK(!cmd::run("asset.texture", {"a.recipe", ""}).ok);

    // The extension is load-bearing, not decoration: it is what tells the rest of the
    // project the file is readable at runtime.
    CHECK(!cmd::run("asset.texture", {"textures/farm_water.recipe", "textures/x.png"}).ok);
    CHECK(!cmd::run("asset.texture", {"nosuch.recipe", "textures/x.hrt"}).ok);

    // The refusal this command needed a new rule for. `from_recipe` cannot fail — every
    // missing key keeps its default, which is what makes the format forward-compatible.
    // For a command that WRITES, that tolerance is a trap: pointed at the wrong file it
    // would bake the DEFAULT texture over the destination and report success.
    // Start from a known-absent destination: a leftover from an earlier run would make
    // "it wrote nothing" pass by accident, which is the same lie as a shared service.
    std::filesystem::remove(ASSET_ROOT "/assets/textures/_junk_out.hrt");

    // Two shapes of not-a-recipe, and they fail for different reasons inside the
    // parser. Lines with no `=` never reach the key chain at all; lines WITH one reach
    // it and match nothing. Only the second exercises the unknown-key branch — testing
    // just the first left that branch deletable with every test still green.
    for (const char* junk_text : {"hi\nthere\n", "colour=blue\nname=pond\n"}) {
        CHECK(assets::write_file("textures/_not_a.recipe",
                                 std::vector<std::uint8_t>(junk_text, junk_text + std::strlen(junk_text))));
        const engine::OpResult junk = cmd::run("asset.texture", {"textures/_not_a.recipe",
                                                                 "textures/_junk_out.hrt"});
        CHECK(!junk.ok);
        CHECK(junk.message.find("recognised") != std::string::npos);
        CHECK(!assets::load_file("textures/_junk_out.hrt"));   // and it wrote NOTHING
    }
    std::filesystem::remove(ASSET_ROOT "/assets/textures/_not_a.recipe");

    // The happy path, against the recipe the farm actually ships: baking it again
    // reproduces the committed bytes exactly. That is what makes a `.recipe` evidence
    // rather than a comment — the pond tile can be regenerated by anyone.
    const auto committed = assets::load_file("textures/farm_water.hrt");
    CHECK(committed.has_value());
    const engine::OpResult ok = cmd::run("asset.texture", {"textures/farm_water.recipe",
                                                           "textures/_rebake.hrt"});
    CHECK(ok.ok);
    const auto rebaked = assets::load_file("textures/_rebake.hrt");
    CHECK(rebaked.has_value());
    if (committed && rebaked) CHECK(*committed == *rebaked);
    std::filesystem::remove(ASSET_ROOT "/assets/textures/_rebake.hrt");

    // ---- the third door ----
    CHECK(!cmd::run("asset.pixels", {}).ok);
    CHECK(!cmd::run("asset.pixels", {"a.pix"}).ok);
    CHECK(!cmd::run("asset.pixels", {"", "b.hrt"}).ok);
    CHECK(!cmd::run("asset.pixels", {"a.pix", ""}).ok);
    CHECK(!cmd::run("asset.pixels", {"textures/farm_path.pix", "textures/x.png"}).ok);
    CHECK(!cmd::run("asset.pixels", {"nosuch.pix", "textures/x.hrt"}).ok);

    // Pointed at a file that is not a sheet it must write NOTHING. Unlike a recipe
    // this format cannot silently default — but "cannot default" is a claim about the
    // parser, and the thing worth pinning here is that the FILE on disk is untouched.
    std::filesystem::remove(ASSET_ROOT "/assets/textures/_junk_pix.hrt");
    static const char kNotAPix[] = "size 4\ngrid 1 1\npalette . 00000000\ntile 0\n....\n";
    CHECK(assets::write_file("textures/_not_a.pix",
                             std::vector<std::uint8_t>(kNotAPix, kNotAPix + std::strlen(kNotAPix))));
    const engine::OpResult short_tile = cmd::run("asset.pixels", {"textures/_not_a.pix",
                                                                  "textures/_junk_pix.hrt"});
    CHECK(!short_tile.ok);
    CHECK(short_tile.message.find("line ") != std::string::npos);   // and it says WHERE
    CHECK(!assets::load_file("textures/_junk_pix.hrt"));
    std::filesystem::remove(ASSET_ROOT "/assets/textures/_not_a.pix");

    // The happy path, against the sheet the farm actually ships: the committed .hrt is
    // what the committed .pix bakes to. Same rule as the pond's .recipe — art in this
    // repo is reproducible from a source that can be read and changed, not a binary
    // somebody once made.
    const auto committed_path = assets::load_file("textures/farm_path.hrt");
    CHECK(committed_path.has_value());
    const engine::OpResult drew = cmd::run("asset.pixels", {"textures/farm_path.pix",
                                                            "textures/_repix.hrt"});
    CHECK(drew.ok);
    const auto redrawn = assets::load_file("textures/_repix.hrt");
    CHECK(redrawn.has_value());
    if (committed_path && redrawn) CHECK(*committed_path == *redrawn);
    std::filesystem::remove(ASSET_ROOT "/assets/textures/_repix.hrt");
}

// -----------------------------------------------------------------------------
//  The import door, held to the same standard as the other two.
//
//  A `.recipe` and a `.pix` were each re-baked and byte-compared above; the import
//  never was. It was the one door whose output had to be taken on trust, which is an
//  odd place for the trust to sit — it is the door with a LICENCE behind it. This
//  reads the claims out of the packs rather than naming Kenney, so a second pack is
//  covered the day it is added and not the day somebody remembers to widen a test.
// -----------------------------------------------------------------------------
static void test_every_import_reproduces_its_committed_bytes() {
    assets::set_base_path(ASSET_ROOT "/assets");
    cmd::clear();
    cmd::register_asset_commands();

    int checked = 0;
    for (const auto& p : assets::list_tree("", ".pack")) {
        const auto bytes = assets::load_file(p);
        CHECK(bytes.has_value());
        if (!bytes) continue;
        const auto pack = engine::parse_pack(std::string(bytes->begin(), bytes->end()));
        CHECK(pack.has_value());
        if (!pack) continue;

        for (const auto& imp : pack->imports) {
            const auto committed = assets::load_file(imp.second);
            CHECK(committed.has_value());          // the pack claims a file that is there
            const engine::OpResult r = cmd::run("asset.import", {imp.first, "textures/_reimport.hrt"});
            CHECK(r.ok);
            if (!r.ok) std::printf("      %s: %s\n", imp.first.c_str(), r.message.c_str());
            const auto again = assets::load_file("textures/_reimport.hrt");
            CHECK(again.has_value());
            if (committed && again) CHECK(*committed == *again);
            std::filesystem::remove(ASSET_ROOT "/assets/textures/_reimport.hrt");
            ++checked;
        }
    }
    // A loop over an empty list passes every assertion inside it. Say out loud that it
    // ran, or "all imports reproduce" becomes true by finding no imports.
    CHECK(checked >= 1);
}

// -----------------------------------------------------------------------------
//  Bringing an asset into existence — the ceiling chapter 127 wrote down.
//
//  Run against a TEMPORARY asset root, not the repository's. `asset.new` re-bakes
//  ATTRIBUTION.md as its last act, which is the point of it; pointed at the real
//  assets/ it would rewrite a committed file and leave the tree dirty after a test
//  run, and deleting the files afterwards would leave the ledger listing assets that
//  no longer exist. A test whose cleanup is a second chance to be wrong is the same
//  trap as a shared service (see the local-services lesson in test_baas_*).
// -----------------------------------------------------------------------------
static void test_asset_new() {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "gd_asset_new_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "textures");
    assets::set_base_path(root.string());
    cmd::clear();
    cmd::register_asset_commands();

    // ---- the name becomes a path, so the name is a trust boundary ----
    for (const char* bad : {"../evil", "a/b", "a\\b", "a.b", "", "with space"}) {
        const engine::OpResult r = cmd::run("asset.new", {bad, "16", "1", "1"});
        CHECK(!r.ok);
    }
    CHECK(!cmd::run("asset.new", {"ok", "16", "1"}).ok);          // too few arguments
    CHECK(!cmd::run("asset.new", {"ok", "abc", "1", "1"}).ok);    // not numbers
    CHECK(!cmd::run("asset.new", {"ok", "0", "1", "1"}).ok);      // no zero dimension
    CHECK(!cmd::run("asset.new", {"ok", "16", "0", "1"}).ok);
    CHECK(!cmd::run("asset.new", {"ok", "-4", "2", "2"}).ok);
    // The multiplication is guarded BEFORE it happens: this is a several-gigabyte
    // allocation, not an error message, if the check comes after.
    CHECK(!cmd::run("asset.new", {"ok", "16", "99999", "99999"}).ok);
    CHECK(std::filesystem::is_empty(root / "textures"));          // and nothing was written

    // ---- the happy path ----
    const engine::OpResult made = cmd::run("asset.new", {"signs", "16", "2", "2"});
    CHECK(made.ok);
    if (!made.ok) std::printf("      %s\n", made.message.c_str());
    CHECK(assets::load_file("textures/signs.pix").has_value());
    CHECK(assets::load_file("textures/signs.hrt").has_value());
    // Born as a SOURCE: the `.hrt` is what the `.pix` bakes to, exactly as for a
    // file a person typed, which is what makes the ledger call it `drawn` with no
    // special case for "the Studio made this one".
    {
        const auto pix = assets::load_file("textures/signs.pix");
        const auto hrt = assets::load_file("textures/signs.hrt");
        CHECK(pix && hrt);
        if (pix && hrt) {
            std::string why;
            const auto img = paint::bake_pixels(std::string(pix->begin(), pix->end()), &why);
            CHECK(img.has_value());
            if (img) {
                CHECK(img->w == 32 && img->h == 32);
                CHECK(gfx::encode_hrt(*img) == *hrt);
                for (gfx::Color c : img->pixels) CHECK((c >> 24) == 0);   // blank means blank
            }
        }
    }
    // Creating is not editing: it never lands on a file that is already there.
    CHECK(!cmd::run("asset.new", {"signs", "16", "2", "2"}).ok);

    // ---- refused AFTER the name checks pass must still write nothing ----
    const engine::OpResult bad_proj =
        cmd::run("asset.new", {"later", "16", "1", "1", "nosuch.gameproject"});
    CHECK(!bad_proj.ok);
    CHECK(bad_proj.message.find("nothing was created") != std::string::npos);
    CHECK(!assets::load_file("textures/later.pix"));   // <- the half-done creation
    CHECK(!assets::load_file("textures/later.hrt"));

    // ---- declared in the manifest, or the project cannot see it ----
    static const char kManifest[] = "gameproject1\nname T\nschema 1\nentry fps\n";
    std::filesystem::create_directories(root / "projects");
    CHECK(assets::write_file("projects/t.gameproject",
                             std::vector<std::uint8_t>(kManifest, kManifest + std::strlen(kManifest))));
    const engine::OpResult declared =
        cmd::run("asset.new", {"road", "8", "1", "1", "projects/t.gameproject"});
    CHECK(declared.ok);
    const auto mf = assets::load_file("projects/t.gameproject");
    CHECK(mf.has_value());
    if (mf) {
        const std::string text(mf->begin(), mf->end());
        CHECK(text.find("asset texture textures/road.hrt") != std::string::npos);
        CHECK(text.find("entry fps") != std::string::npos);      // and it rewrote nothing else
    }

    // ---- and the ledger, without anyone remembering ----
    // Said out loud when there is no document, rather than skipped quietly: the whole
    // slice is about a rule nobody should have to hold in their head.
    CHECK(declared.message.find("no ATTRIBUTION.md") != std::string::npos);

    static const char kDoc[] = "# A\n\n<!-- BEGIN LEDGER (generated) -->\n<!-- END LEDGER (generated) -->\n";
    CHECK(assets::write_file("ATTRIBUTION.md",
                             std::vector<std::uint8_t>(kDoc, kDoc + std::strlen(kDoc))));
    const engine::OpResult with_doc = cmd::run("asset.new", {"fence", "8", "1", "1"});
    CHECK(with_doc.ok);
    CHECK(with_doc.message.find("ledger re-baked") != std::string::npos);
    const auto doc = assets::load_file("ATTRIBUTION.md");
    CHECK(doc.has_value());
    if (doc) {
        const std::string text(doc->begin(), doc->end());
        CHECK(text.find("`textures/fence.hrt` | drawn") != std::string::npos);
        CHECK(text.find("`textures/signs.hrt` | drawn") != std::string::npos);  // and its siblings
        CHECK(text.find("UNRECORDED") == std::string::npos);
        CHECK(text.find("# A") != std::string::npos);            // hand-written prose kept
    }

    std::filesystem::remove_all(root);
    assets::set_base_path(ASSET_ROOT "/assets");
}

// -----------------------------------------------------------------------------
//  The ledger command. It writes a COMMITTED file, so its refusals matter more than
//  its happy path — which test_provenance already pins byte-for-byte.
// -----------------------------------------------------------------------------
static void test_attribution_command() {
    assets::set_base_path(ASSET_ROOT "/assets");
    cmd::clear();
    cmd::register_asset_commands();

    CHECK(!cmd::run("asset.attribution", {}).ok);        // D17: no blank arguments
    CHECK(!cmd::run("asset.attribution", {""}).ok);
    CHECK(!cmd::run("asset.attribution", {"nosuch.md"}).ok);

    // A document with no markers is refused and left ALONE. Appending would give the
    // file two ledgers, and the file it would do that to is one somebody wrote.
    static const char kProse[] = "# hand written\n\nno markers here\n";
    CHECK(assets::write_file("_no_markers.md",
                             std::vector<std::uint8_t>(kProse, kProse + std::strlen(kProse))));
    const engine::OpResult r = cmd::run("asset.attribution", {"_no_markers.md"});
    CHECK(!r.ok);
    CHECK(r.message.find("marker") != std::string::npos);
    const auto after = assets::load_file("_no_markers.md");
    CHECK(after.has_value());
    if (after) CHECK(std::string(after->begin(), after->end()) == kProse);   // untouched
    std::filesystem::remove(ASSET_ROOT "/assets/_no_markers.md");
}

int main() {
    test_registry();
    test_asset_commands();
    test_every_import_reproduces_its_committed_bytes();
    test_attribution_command();
    test_asset_new();
    test_filter_and_unregister();
    test_release_commands();
    test_inspect_command();
    if (g_failures == 0) std::printf("commands: all tests passed\n");
    else                 std::printf("commands: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
