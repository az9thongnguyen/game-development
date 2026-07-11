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
        "gameproject1\nname X\nschema 1\nentry fps\nassets content/\nbaas http://x\n";
    auto p = parse_project(text);
    CHECK(p && p->name == "X" && p->entry == "fps" && p->schema == 1);
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

int main() {
    test_parse_and_roundtrip();
    test_fail_closed();
    test_forward_compat();
    test_validate();

    if (g_failures == 0) std::printf("project: all tests passed\n");
    else                 std::printf("project: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
