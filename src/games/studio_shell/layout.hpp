// =============================================================================
//  games/studio_shell/layout.hpp  —  where the Studio's divider sits, on disk
// =============================================================================
//  Chapter 112 left a comment where the split between canvas and inspector is
//  computed: "The split is fixed — a draggable one needs a cursor shape, a hit zone
//  and a persisted position, and no second author has asked for it yet." Chapter 144
//  is that author. This file is the third of the three things.
//
//  Header-only and pure: no renderer, no `assets::`, no window. Reading and writing
//  the file is the shell's job (everything goes through `assets::`), and this is only
//  the text ↔ value translation plus the ONE clamp. That matters because the clamp is
//  the whole design:
//
//      the STORED width is never clamped; the DRAWN width always is.
//
//  A width that fits a 1280px window does not fit a 700px one. Clamping on the way in
//  would rewrite the file the first time you resized the window small — and the width
//  you spent time dragging would be gone for good, silently, with the window still
//  showing you something reasonable. Guards have a reverse direction: the bug is never
//  the collision the guard prevents, it is the guard never lifting.
// =============================================================================
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "engine/assets.hpp"

namespace studioshell {

// A panel narrower than this is a scroll bar with ambitions. Below it the workspace's
// own controls start wrapping, which the Pixels editor already announces on the status
// strip ("inspector clipped").
inline constexpr int kMinInspector = 180;

// The DRAWN width. `stored` is what the author dragged it to and `body_w` is what the
// window has to give today.
[[nodiscard]] inline int fit_inspector(int stored, int body_w) {
    // The canvas never gets thinner than the panel beside it: an editor whose subject
    // is smaller than its controls has its priorities backwards.
    const int most  = body_w / 2;
    const int least = std::min(kMinInspector, most);
    if (most <= 0) return 0;
    return std::clamp(stored, least, most);
}

struct Layout {
    // Per WORKSPACE, not one global number. The interface's own header says why: "260
    // suits a tile palette; an actor inspector with sliders wants more." One shared
    // divider would make every tab switch move it.
    std::map<std::string, int> inspector;

    [[nodiscard]] int width_for(const std::string& name, int fallback) const {
        const auto it = inspector.find(name);
        return it == inspector.end() ? fallback : it->second;
    }
    void set(const std::string& name, int w) { inspector[name] = w; }
};

inline constexpr int kLayoutVersion = 1;

[[nodiscard]] inline std::string to_text(const Layout& l) {
    std::ostringstream out;
    out << "studiolayout " << kLayoutVersion << "\n";
    // std::map iterates sorted, so the same layout is the same bytes — the rule every
    // other generated file here follows.
    for (const auto& [name, w] : l.inspector) out << "inspector " << name << " " << w << "\n";
    return out.str();
}

// Refused rather than half-read when the version is from the future, for the reason
// map2 gives (chapter 134): a file nobody can write wrongly is better than one that
// half-loads. An unknown KEY is skipped, though — this file will grow.
[[nodiscard]] inline std::optional<Layout> parse_layout(const std::string& text) {
    std::istringstream in(text);
    std::string        word;
    int                version = 0;
    if (!(in >> word >> version) || word != "studiolayout") return std::nullopt;
    if (version < 1 || version > kLayoutVersion) return std::nullopt;

    Layout l;
    std::string line;
    std::getline(in, line);   // finish the header line
    while (std::getline(in, line)) {
        std::istringstream ln(line);
        std::string        key, name;
        int                value = 0;
        if (!(ln >> key)) continue;
        if (key != "inspector") continue;
        if (!(ln >> name >> value)) return std::nullopt;
        if (value <= 0) return std::nullopt;   // a zero-wide panel is not a preference
        l.inspector[name] = value;
    }
    return l;
}

// ---- the half that touches the disk ---------------------------------------------
// Here rather than in each caller: the Studio's Edit tab and the full-screen lab are
// the SAME workspace objects (that is the rule the host exists to enforce), so a
// divider dragged in one and forgotten by the other would be the same class of bug as
// a control drawn in one place and hit in another. Two call sites, one path, one file.
inline constexpr const char* kLayoutPath = "saves/studio.layout";

[[nodiscard]] inline Layout read_layout() {
    const auto bytes = assets::load_file(kLayoutPath);
    if (!bytes) return Layout{};
    const auto parsed = parse_layout(std::string(bytes->begin(), bytes->end()));
    // A layout file we cannot read is not worth an error: the answer is the default
    // layout, which is what a first run gets anyway.
    return parsed ? *parsed : Layout{};
}

inline bool write_layout(const Layout& l) {
    const std::string text = to_text(l);
    return assets::write_file(kLayoutPath,
                              std::vector<std::uint8_t>(text.begin(), text.end()));
}

}  // namespace studioshell
