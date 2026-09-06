// =============================================================================
//  tests/test_provenance.cpp  —  the ledger, and the hole it exists to find
// =============================================================================
//  Three halves, checked differently:
//
//   * parse/to_text/attribute/ledger_markdown are PURE — data in, data out, and the
//     rendered table is committed inside ATTRIBUTION.md, so its bytes are a format.
//
//   * `attribute` is checked most heavily in the NEGATIVE: an unrecorded file, a
//     stale claim, a doubly-claimed file. A ledger that only ever reports success is
//     the blanket paragraph this whole subsystem replaced.
//
//   * the last two run against the REAL assets/ tree, which is what makes the rule
//     in CLAUDE.md enforceable: bake a new `.hrt` with no source and no pack line and
//     this suite goes red, rather than a licence question going unnoticed.
// =============================================================================
#include "engine/asset/provenance.hpp"

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

// ---- parse / emit -----------------------------------------------------------

static void test_pack_roundtrip() {
    const std::string src =
        "assetpack1\n"
        "name Kenney — Tiny Town 1.1\n"
        "author Kenney\n"
        "source https://kenney.nl/assets/tiny-town\n"
        "licence CC0-1.0\n"
        "licence_url http://creativecommons.org/publicdomain/zero/1.0/\n"
        "added 2026-09-04\n"
        "note two words, and a comma\n"
        "import textures/a.png textures/a.hrt\n"
        "file pieces/wK.hrt\n";

    auto p = parse_pack(src);
    CHECK(p.has_value());
    if (!p) return;
    CHECK(p->name == "Kenney — Tiny Town 1.1");   // free text, spaces and UTF-8 kept
    CHECK(p->note == "two words, and a comma");
    CHECK(p->imports.size() == 1);
    CHECK(p->imports[0].first == "textures/a.png" && p->imports[0].second == "textures/a.hrt");
    CHECK(p->files.size() == 1 && p->files[0] == "pieces/wK.hrt");
    CHECK(p->path.empty());     // a file does not carry its own name; scan sets this
    // The round trip is the migration story: a pack read and written back is the same
    // file, so re-baking the ledger never rewrites the packs.
    CHECK(to_text(*p) == src);
}

static void test_pack_fails_closed_and_ignores_unknowns() {
    CHECK(!parse_pack("").has_value());                       // no magic
    CHECK(!parse_pack("name X\nassetpack1\n").has_value());   // magic must come first
    CHECK(!parse_pack("assetpack2\nname X\n").has_value());   // a version we do not know

    auto p = parse_pack("assetpack1\nfuture_key whatever\nfile a.hrt\n");
    CHECK(p.has_value());                                     // additive fields survive
    if (p) CHECK(p->files.size() == 1);

    // A malformed structured line is dropped, not half-accepted: an import with no
    // destination would otherwise claim the empty path.
    auto q = parse_pack("assetpack1\nimport only_one_token\nfile\n");
    CHECK(q.has_value());
    if (q) CHECK(q->imports.empty() && q->files.empty());

    // Absent fields are not written back, so a pack that never heard of `note` does
    // not grow one the first time anything saves it.
    Pack bare;
    bare.files.push_back("a.hrt");
    CHECK(to_text(bare) == "assetpack1\nfile a.hrt\n");
}

// ---- deciding ---------------------------------------------------------------

static Pack kenney() {
    Pack p;
    p.name    = "Kenney";
    p.licence = "CC0-1.0";
    p.imports.emplace_back("textures/k.png", "textures/town.hrt");
    return p;
}

static void test_the_three_doors_leave_three_marks() {
    const std::vector<std::string> hrt = {
        "textures/drawn.hrt", "textures/gen.hrt", "textures/town.hrt"};
    const std::vector<std::string> sources = {
        "textures/drawn.pix", "textures/gen.recipe"};

    const Ledger l = attribute(hrt, sources, {kenney()});
    CHECK(l.ok());
    CHECK(l.unrecorded() == 0);
    CHECK(l.files.size() == 3);

    CHECK(l.files[0].origin == Origin::Drawn);
    CHECK(l.files[0].source == "textures/drawn.pix");
    CHECK(l.files[0].licence.empty());            // ours: the repository's own

    CHECK(l.files[1].origin == Origin::Generated);
    CHECK(l.files[1].source == "textures/gen.recipe");

    CHECK(l.files[2].origin == Origin::Imported);
    CHECK(l.files[2].source == "textures/k.png");  // the file it was baked FROM
    CHECK(l.files[2].licence == "CC0-1.0");
    CHECK(l.files[2].pack == "Kenney");
}

static void test_a_file_with_no_source_is_the_failure() {
    const Ledger l = attribute({"textures/orphan.hrt"}, {}, {});
    CHECK(!l.ok());                                   // <- the whole reason this exists
    CHECK(l.unrecorded() == 1);
    CHECK(l.files.size() == 1);                       // listed, not hidden
    CHECK(l.files[0].origin == Origin::Unrecorded);
    CHECK(contains(ledger_markdown(l), "UNRECORDED"));

    // ...and the same file, once declared, is fine — the guard's other direction.
    Pack ours;
    ours.name = "Ours";
    ours.path = "ours.pack";
    ours.files.push_back("textures/orphan.hrt");
    const Ledger d = attribute({"textures/orphan.hrt"}, {}, {ours});
    CHECK(d.ok());
    CHECK(d.files[0].origin == Origin::Declared);
    // A declared asset bakes from nothing, so its source is the file that VOUCHES —
    // the one to open when the question is who said so.
    CHECK(d.files[0].source == "ours.pack");
}

static void test_a_sibling_of_the_wrong_kind_is_not_a_source() {
    // `town.png` sitting next to `town.hrt` proves nothing: the import door is a
    // `.pack` line, precisely so a stray PNG cannot silently vouch for a file.
    const Ledger l = attribute({"textures/town.hrt"}, {"textures/town.png"}, {});
    CHECK(l.unrecorded() == 1);
}

static void test_a_stale_claim_is_a_problem_not_coverage() {
    Pack p;
    p.name = "Ours";
    p.files.push_back("textures/deleted.hrt");
    const Ledger l = attribute({}, {}, {p});
    CHECK(!l.ok());
    CHECK(l.problems.size() == 1);
    CHECK(contains(l.problems[0], "deleted.hrt"));
    CHECK(contains(l.problems[0], "not an asset here"));
}

static void test_two_answers_is_not_an_answer() {
    Pack p;
    p.name = "Ours";
    p.files.push_back("textures/both.hrt");
    const Ledger l = attribute({"textures/both.hrt"}, {"textures/both.pix"}, {p});
    CHECK(!l.ok());                                   // claimed AND drawn
    CHECK(l.problems.size() == 1);
    CHECK(contains(l.problems[0], "2 origins"));
    CHECK(l.unrecorded() == 0);                       // it is not a HOLE, it is a conflict
}

static void test_markdown_is_a_format() {
    const Ledger l = attribute({"textures/drawn.hrt"}, {"textures/drawn.pix"}, {});
    const std::string md = ledger_markdown(l);
    CHECK(md.substr(0, 9) == "| Asset |");            // header first, always
    CHECK(contains(md, "| `textures/drawn.hrt` | drawn | `textures/drawn.pix` | this repository |"));
    CHECK(contains(md, "1 raster assets, 0 unrecorded."));
    CHECK(md.back() == '\n');
}

static void test_splice_replaces_only_between_the_markers() {
    const std::string doc = std::string("prose above\n\n") + kLedgerBegin +
                            "\n\nOLD TABLE\n" + kLedgerEnd + "\n\nprose below\n";
    auto out = splice_ledger(doc, "NEW TABLE");
    CHECK(out.has_value());
    if (out) {
        CHECK(contains(*out, "prose above"));         // hand-written prose survives
        CHECK(contains(*out, "prose below"));
        CHECK(contains(*out, "NEW TABLE"));
        CHECK(!contains(*out, "OLD TABLE"));
        // Idempotent: baking twice is the same file, which is what lets a test
        // compare bytes rather than compare "roughly".
        auto again = splice_ledger(*out, "NEW TABLE");
        CHECK(again.has_value() && *again == *out);
    }
    // A document with no markers is refused rather than appended to: appending would
    // silently give the file two ledgers.
    CHECK(!splice_ledger("no markers here\n", "X").has_value());
    CHECK(!splice_ledger(std::string(kLedgerEnd) + "\n" + kLedgerBegin, "X").has_value());
}

// ---- the real tree ----------------------------------------------------------

static void test_every_hrt_in_this_repo_is_accounted_for() {
    assets::set_base_path(ASSET_ROOT);
    const Ledger l = scan_provenance();

    CHECK(!l.files.empty());                          // a scan that found nothing is a bug
    for (const auto& p : l.problems) std::printf("      problem: %s\n", p.c_str());
    for (const auto& f : l.files)
        if (f.origin == Origin::Unrecorded)
            std::printf("      unrecorded: %s\n", f.path.c_str());

    CHECK(l.ok());
    if (!l.ok())
        std::printf("      a .hrt arrived with no source and no pack line.\n"
                    "      Give it a .pix/.recipe sibling, or a `file` line in a .pack.\n");

    // The scan must see the whole tree, not one folder — `pieces/` and `sprites/`
    // are the files a `textures/`-only walk would have missed.
    bool saw_pieces = false, saw_sprites = false, saw_root = false;
    for (const auto& f : l.files) {
        if (f.path.rfind("pieces/", 0) == 0)  saw_pieces = true;
        if (f.path.rfind("sprites/", 0) == 0) saw_sprites = true;
        if (f.path.find('/') == std::string::npos) saw_root = true;
    }
    CHECK(saw_pieces);
    CHECK(saw_sprites);
    CHECK(saw_root);

    // Sorted, because the ledger is spliced into a committed file.
    for (std::size_t i = 1; i < l.files.size(); ++i)
        CHECK(l.files[i - 1].path < l.files[i].path);
}

static void test_committed_attribution_is_what_the_tree_bakes_to() {
    assets::set_base_path(ASSET_ROOT);
    auto doc = assets::load_file("ATTRIBUTION.md");
    CHECK(doc.has_value());
    if (!doc) return;

    const std::string have(doc->begin(), doc->end());
    auto want = splice_ledger(have, ledger_markdown(scan_provenance()));
    CHECK(want.has_value());
    if (!want) {
        std::printf("      ATTRIBUTION.md lost its generated markers\n");
        return;
    }
    CHECK(*want == have);
    if (*want != have)
        std::printf("      ATTRIBUTION.md is stale — re-bake:\n"
                    "      ./build/demo --cmd asset.attribution ATTRIBUTION.md\n");
}

int main() {
    test_pack_roundtrip();
    test_pack_fails_closed_and_ignores_unknowns();
    test_the_three_doors_leave_three_marks();
    test_a_file_with_no_source_is_the_failure();
    test_a_sibling_of_the_wrong_kind_is_not_a_source();
    test_a_stale_claim_is_a_problem_not_coverage();
    test_two_answers_is_not_an_answer();
    test_markdown_is_a_format();
    test_splice_replaces_only_between_the_markers();
    test_every_hrt_in_this_repo_is_accounted_for();
    test_committed_attribution_is_what_the_tree_bakes_to();

    if (g_failures == 0) std::printf("test_provenance: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
