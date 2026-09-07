// =============================================================================
//  tests/test_adr_index.cpp  —  the decision index, and the three ways it rots
// =============================================================================
//  `docs/adr/README.md` is an index of architectural decisions that points into
//  `docs/book/`. It carries no prose of its own on purpose: the chapter is where the
//  argument lives, and a second copy of an argument is a second thing to forget.
//
//  What an index like this actually fails at is never the prose. It is:
//
//    1. a link to a chapter that has been renamed or never existed;
//    2. an id used twice, so "superseded by 22" is ambiguous;
//    3. a `Superseded by N` pointing at a row that is not there.
//
//  All three are mechanical, so all three are checked here rather than trusted. This
//  is the same move as `test_baas_sql_seam` reading the source tree and
//  `engine::scan_provenance` deriving the attribution ledger: a rule nobody counts is
//  a rule this repository has already broken.
// =============================================================================
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("FAIL %s:%d:  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

namespace {

bool exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// The cells of a markdown table row, trimmed. Empty for anything that is not one.
std::vector<std::string> cells(const std::string& line) {
    std::vector<std::string> out;
    if (line.size() < 2 || line.front() != '|') return out;
    std::string cur;
    for (std::size_t i = 1; i < line.size(); ++i) {
        // `\|` is markdown's escaped pipe and belongs to the CELL, not to the table.
        // Decision 20 names a file header with three of them in it, and a splitter
        // that did not know that read the row as nine columns of nonsense.
        if (line[i] == '\\' && i + 1 < line.size() && line[i + 1] == '|') {
            cur += '|';
            ++i;
            continue;
        }
        if (line[i] == '|') {
            const auto b = cur.find_first_not_of(" \t");
            const auto e = cur.find_last_not_of(" \t");
            out.push_back(b == std::string::npos ? "" : cur.substr(b, e - b + 1));
            cur.clear();
        } else {
            cur += line[i];
        }
    }
    return out;
}

bool all_digits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}

}  // namespace

int main() {
    const std::string root  = REPO_ROOT;
    const std::string index = root + "/docs/adr/README.md";

    std::ifstream in(index);
    if (!in.good()) {
        std::printf("FAIL cannot read %s\n", index.c_str());
        return 1;
    }

    std::string        line;
    int                rows = 0, links = 0;
    std::set<int>      ids;
    std::vector<int>   superseded_targets;
    std::set<std::string> missing;

    while (std::getline(in, line)) {
        // ---- 1. every chapter this file points at exists ----------------------
        for (std::size_t at = line.find("../book/"); at != std::string::npos;
             at = line.find("../book/", at + 1)) {
            const std::size_t end = line.find(')', at);
            if (end == std::string::npos) continue;
            const std::string rel = line.substr(at, end - at);
            ++links;
            if (!exists(root + "/docs/adr/" + rel)) missing.insert(rel);
        }

        const auto c = cells(line);
        if (c.size() < 4 || !all_digits(c[0])) continue;
        ++rows;

        // ---- 2. an id is used once ------------------------------------------
        const int id = std::stoi(c[0]);
        if (!ids.insert(id).second) {
            std::printf("FAIL duplicate decision id %d\n", id);
            ++g_failures;
        }

        // ---- 3. `Superseded by N` names a row that is there -------------------
        const std::string status = c[3];
        const std::size_t sup    = status.find("Superseded by ");
        if (sup != std::string::npos) {
            std::string num;
            for (std::size_t i = sup + 14; i < status.size() && status[i] >= '0' &&
                                           status[i] <= '9'; ++i)
                num += status[i];
            if (num.empty()) {
                std::printf("FAIL decision %d: \"%s\" names no id\n", id, status.c_str());
                ++g_failures;
            } else {
                superseded_targets.push_back(std::stoi(num));
            }
        } else if (status.rfind("Accepted", 0) != 0) {
            std::printf("FAIL decision %d: unknown status \"%s\"\n", id, status.c_str());
            ++g_failures;
        }
    }

    for (const auto& m : missing) {
        std::printf("FAIL the index points at a chapter that is not there: %s\n", m.c_str());
        ++g_failures;
    }
    for (int t : superseded_targets)
        if (!ids.count(t)) {
            std::printf("FAIL \"Superseded by %d\" — there is no decision %d\n", t, t);
            ++g_failures;
        }

    std::printf("  %d decisions, %d chapter links, %zu superseded\n", rows, links,
                superseded_targets.size());
    // Floors, so an index that lost its table cannot pass as an index with no problems.
    CHECK(rows >= 30);
    CHECK(links >= 30);
    CHECK(!superseded_targets.empty());

    if (g_failures == 0) std::printf("adr_index: all tests passed\n");
    else                 std::printf("adr_index: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
