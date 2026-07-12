// =============================================================================
//  tests/test_hub.cpp  —  hub "next recommended action" (dependency-free, CTest)
// =============================================================================
//  Locks the hub's decision brain: from a project's aggregate state, it names the one
//  right next step along create → publish → promote → in-sync, and surfaces the first
//  blocking problem when the project isn't shippable.
// =============================================================================
#include "engine/hub/hub.hpp"

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

// A shippable view with the three channels in various states.
static HubView shippable(const std::string& localHash,
                         const std::string& dev, const std::string& preview, const std::string& prod) {
    HubView v;
    v.name = "Demo"; v.entry = "fps"; v.schema = 1;
    v.shippable = true;
    v.local_package = localHash;
    auto ch = [&](const std::string& n, const std::string& rel) {
        return HubChannel{n, rel, !rel.empty(), !rel.empty() && rel == localHash};
    };
    v.channels = {ch("development", dev), ch("preview", preview), ch("production", prod)};
    return v;
}

int main() {
    const std::string H = "cbf29ce484222325";
    const std::string OLD = "244d6ca9ecf5439e";

    // Not shippable → fix the first problem.
    HubView bad;
    bad.shippable = false;
    bad.problems = {"missing asset: maps/x.map", "unknown entry 'zzz'"};
    CHECK(recommend(bad) == "fix: missing asset: maps/x.map");

    // Not shippable, no explicit problem → generic fix message.
    HubView bad2; bad2.shippable = false;
    CHECK(recommend(bad2) == "fix: project is not shippable");

    // Shippable, nothing published → publish.
    CHECK(recommend(shippable(H, "", "", "")) == "publish: your source is not yet the development release");

    // Shippable, development holds an OLD release (source drifted) → publish again.
    CHECK(recommend(shippable(H, OLD, "", "")) == "publish: your source is not yet the development release");

    // Development matches source, preview behind → promote to preview.
    CHECK(recommend(shippable(H, H, "", "")) == "promote: development -> preview");
    CHECK(recommend(shippable(H, H, OLD, "")) == "promote: development -> preview");

    // Preview caught up, production behind → promote to production.
    CHECK(recommend(shippable(H, H, H, "")) == "promote: preview -> production");
    CHECK(recommend(shippable(H, H, H, OLD)) == "promote: preview -> production");

    // Everything in sync.
    CHECK(recommend(shippable(H, H, H, H)) == "in sync: production matches your source");

    // --- hub_lines: same content the CLI and the Scene both render ---
    auto lines = hub_lines(shippable(H, H, H, ""));
    CHECK(!lines.empty());
    CHECK(lines.front() == "Hub  Demo");                         // title first
    CHECK(lines.back() == "next: promote: preview -> production"); // recommendation last
    bool has_pkg = false, has_dev = false;
    for (const auto& l : lines) {
        if (l == "package " + H) has_pkg = true;
        if (l.rfind("development", 0) == 0 && l.find("==source") != std::string::npos) has_dev = true;
    }
    CHECK(has_pkg);   // shippable → package line present
    CHECK(has_dev);   // development points at source → "==source" flag

    // not-shippable view surfaces the problem as a line and recommends fix
    HubView broken; broken.name = "X"; broken.shippable = false; broken.problems = {"missing asset: a.map"};
    auto blines = hub_lines(broken);
    CHECK(blines.back() == "next: fix: missing asset: a.map");
    bool prob_line = false;
    for (const auto& l : blines) if (l == "  - missing asset: a.map") prob_line = true;
    CHECK(prob_line);

    if (g_failures == 0) std::printf("hub: all tests passed\n");
    else                 std::printf("hub: %d FAILURE(S)\n", g_failures);
    return g_failures;
}
