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
#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/commands/release_commands.hpp"

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

int main() {
    test_registry();
    test_filter_and_unregister();
    test_release_commands();
    if (g_failures == 0) std::printf("commands: all tests passed\n");
    else                 std::printf("commands: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
