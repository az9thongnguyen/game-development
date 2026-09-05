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

#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/commands/asset_commands.hpp"
#include "engine/commands/release_commands.hpp"
#include "engine/image.hpp"

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
//  The two doors into `.hrt`. `asset.import` brings a picture in from a format we
//  did not invent; `asset.texture` bakes one this project drew. They matter as a
//  PAIR: a tile from the CC0 pack and a tile from the Texture Lab are the same kind
//  of file by the time anything downstream sees them, and that is the only reason
//  "support both art sources" costs the engine nothing.
// -----------------------------------------------------------------------------
static void test_asset_commands() {
    assets::set_base_path(ASSET_ROOT "/assets");
    cmd::clear();
    cmd::register_asset_commands();
    CHECK(cmd::all().size() == 2);   // both doors registered, and only those

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
}

int main() {
    test_registry();
    test_asset_commands();
    test_filter_and_unregister();
    test_release_commands();
    test_inspect_command();
    if (g_failures == 0) std::printf("commands: all tests passed\n");
    else                 std::printf("commands: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
