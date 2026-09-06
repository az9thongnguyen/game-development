// =============================================================================
//  tests/test_project.cpp  —  game.project manifest core (dependency-free, CTest)
// =============================================================================
//  Locks the parse/validate/round-trip contract the Horizon 0 golden path relies
//  on: a manifest selects the entry scene, malformed input fails closed, and
//  unknown additive keys stay backward-compatible.
// =============================================================================
#include "engine/project/project.hpp"

#include <cstdio>
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

static void test_parse_and_roundtrip() {
    const std::string text = "gameproject1\nname Creator Demo\nschema 1\nentry fps\n";
    auto p = parse_project(text);
    CHECK(p.has_value());
    CHECK(p->name == "Creator Demo");     // free-text name to end of line
    CHECK(p->schema == 1);
    CHECK(p->entry == "fps");
    CHECK(to_text(*p) == text);           // stable, canonical round-trip
    auto p2 = parse_project(to_text(*p));
    CHECK(p2 && p2->name == p->name && p2->entry == p->entry && p2->schema == p->schema);
}

static void test_fail_closed() {
    CHECK(!parse_project("garbage\nname x\n"));            // bad magic
    CHECK(!parse_project(""));                             // empty input
    CHECK(!parse_project("gameproject1\nschema oops\n"));  // malformed schema value
}

static void test_forward_compat() {
    // Unknown additive keys (future fields) must not break known-field parsing.
    const std::string text =
        "gameproject1\nname X\nschema 1\nentry fps\nfuturekey content/\nbaas http://x\n";
    auto p = parse_project(text);
    CHECK(p && p->name == "X" && p->entry == "fps" && p->schema == 1);
    CHECK(p->assets.empty());   // no `asset` lines declared
}

static void test_asset_declarations() {
    // Repeatable `asset <type> <path>` lines populate the dependency list and
    // round-trip in canonical order (after name/schema/entry).
    const std::string text =
        "gameproject1\nname X\nschema 1\nentry fps\n"
        "asset map maps/level_00.map\nasset texture textures/wall_1.hrt\n";
    auto p = parse_project(text);
    CHECK(p && p->assets.size() == 2);
    CHECK(p->assets[0].type == "map" && p->assets[0].path == "maps/level_00.map");
    CHECK(p->assets[1].type == "texture" && p->assets[1].path == "textures/wall_1.hrt");
    CHECK(to_text(*p) == text);   // stable round-trip including assets
}

static void test_validate() {
    const std::vector<std::string> known = {"fps", "studio"};
    CHECK(validate({"Demo", 1, "fps"}, known).empty());               // valid

    auto e_unknown = validate({"Demo", 1, "chess"}, known);           // unknown entry
    CHECK(e_unknown.size() == 1 && e_unknown[0].find("chess") != std::string::npos);

    CHECK(!validate({"", 1, "fps"}, known).empty());                  // missing name
    CHECK(!validate({"Demo", 1, ""}, known).empty());                 // missing entry
    CHECK(!validate({"Demo", kProjectSchema + 1, "fps"}, known).empty());  // schema too new
    CHECK(!validate({"Demo", 0, "fps"}, known).empty());              // schema unset
}

// ---- cover + summary: what a LIST of games needs (chapter 130) ---------------

static void test_cover_and_summary_roundtrip() {
    const std::string text =
        "gameproject1\nname Farm\nschema 1\nentry farm\n"
        "summary Plant, water, sleep. A day at a time.\n"
        "cover textures/town.hrt\n";
    auto p = parse_project(text);
    CHECK(p.has_value());
    CHECK(p->summary == "Plant, water, sleep. A day at a time.");   // free text, spaces kept
    CHECK(p->cover == "textures/town.hrt");
    CHECK(to_text(*p) == text);       // canonical order: name, schema, entry, summary, cover
}

static void test_absent_fields_are_not_written() {
    // The migration IS this test. Every manifest written before these fields existed
    // must round-trip to its own bytes — otherwise the first save rewrites files that
    // did not change, and the round-trip check stops being evidence of anything.
    const std::string old_text = "gameproject1\nname X\nschema 1\nentry fps\n";
    auto p = parse_project(old_text);
    CHECK(p && p->summary.empty() && p->cover.empty());
    CHECK(to_text(*p) == old_text);
    CHECK(validate(*p, {"fps"}).empty());     // absent is valid, not "not yet filled in"
}

static void test_cover_must_be_hrt() {
    // The collection page decodes .hrt by hand; anything else is a blank card, and a
    // blank card is the failure that looks like a slow network.
    auto bad = parse_project("gameproject1\nname X\nschema 1\nentry fps\ncover art/x.png\n");
    CHECK(bad.has_value());                              // it PARSES — this is semantics
    const auto errs = validate(*bad, {"fps"});
    CHECK(errs.size() == 1);
    CHECK(!errs.empty() && errs[0].find("art/x.png") != std::string::npos);

    // Both directions of the guard. A .hrt cover passes, and so does no cover at all;
    // a guard that never lifts is the bug the guard was supposed to prevent.
    auto good = parse_project("gameproject1\nname X\nschema 1\nentry fps\ncover a/b.hrt\n");
    CHECK(good && validate(*good, {"fps"}).empty());
    // ".hrt" alone is a suffix with no name in front of it.
    auto naked = parse_project("gameproject1\nname X\nschema 1\nentry fps\ncover .hrt\n");
    CHECK(naked && validate(*naked, {"fps"}).size() == 1);
}

static void test_summary_is_one_line() {
    // `summary` reads to end of line, so a second line cannot smuggle itself in — it
    // parses as its own (unknown, ignored) record instead of extending the summary.
    auto p = parse_project("gameproject1\nname X\nschema 1\nentry fps\n"
                           "summary first line\nsecond line here\n");
    CHECK(p && p->summary == "first line");
}

int main() {
    test_parse_and_roundtrip();
    test_fail_closed();
    test_forward_compat();
    test_asset_declarations();
    test_validate();
    test_cover_and_summary_roundtrip();
    test_absent_fields_are_not_written();
    test_cover_must_be_hrt();
    test_summary_is_one_line();

    if (g_failures == 0) std::printf("project: all tests passed\n");
    else                 std::printf("project: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
