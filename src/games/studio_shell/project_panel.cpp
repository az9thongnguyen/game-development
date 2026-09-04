// =============================================================================
//  games/studio_shell/project_panel.cpp
// =============================================================================
#include "games/studio_shell/project_panel.hpp"

#include <string>

#include "engine/renderer2d.hpp"
#include "engine/resource/resource.hpp"
#include "engine/ui/theme.hpp"

namespace projectui {

namespace th = ui::theme;

namespace {

// Bytes at human scale. Sizes here span a 40-byte .def and a multi-KB map, and
// "1247" tells you less about which is which than "1.2 KB" does at a glance.
std::string human_bytes(std::size_t n) {
    if (n < 1024) return std::to_string(n) + " B";
    const std::size_t tenths = (n * 10 + 512) / 1024;
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " KB";
}

// One "label: value" row in the detail card, returning the next y.
int field(gfx::Renderer2D& g, int x, int y, int w, const char* label, const std::string& value,
          gfx::Color value_color = th::text) {
    g.set_font_size(th::sz_caption);
    g.draw_text(x, y, label, th::text_muted);
    y += th::sz_caption + th::space_xs;
    g.set_font_size(th::sz_body);
    // Long paths are elided from the FRONT: the tail (the file name) is the part that
    // identifies it, and a path truncated at the end shows you only the directory.
    std::string v = value;
    while (!v.empty() && g.text_width(v.c_str()) > w) v.erase(0, 1);
    if (v != value && v.size() > 1) v = "…" + v.substr(1);
    g.draw_text(x, y, v.c_str(), value_color);
    return y + th::sz_body + th::space_md;
}

}  // namespace

Op draw_project_panel(ui::Context& ui, gfx::Renderer2D& g,
                      const engine::Inspection& in, ui::Rect area, int& selected) {
    Op op = Op::None;
    const int n = static_cast<int>(in.assets.size());
    if (selected >= n) selected = n - 1;      // the list can shrink between inspections
    if (selected < 0)  selected = 0;

    int y = area.y;
    g.set_font_size(th::sz_title);
    g.draw_text(area.x, y, "Project", th::text);

    // The verdict, top-right, as a word and a colour — never colour alone.
    if (!in.parsed) {
        ui.badge(area.x + area.w - 110, y + th::space_xs, "unreadable", ui::Tone::Danger);
    } else if (in.shippable()) {
        ui.badge(area.x + area.w - 110, y + th::space_xs, "shippable", ui::Tone::Success);
    } else {
        const std::string t = std::to_string(in.problems.size()) + " problem" +
                              (in.problems.size() == 1 ? "" : "s");
        ui.badge(area.x + area.w - 110, y + th::space_xs, t.c_str(), ui::Tone::Danger);
    }
    y += th::sz_title + th::space_sm;

    g.set_font_size(th::sz_caption);
    g.draw_text(area.x, y, in.path.c_str(), th::text_muted);
    y += th::sz_caption + th::space_xs;

    if (!in.parsed) {
        // Nothing below has anything to draw: there is no project, only a path that
        // did not lead to one. Say the reason rather than an empty browser.
        g.set_font_size(th::sz_body);
        g.draw_text(area.x, y + th::space_md,
                    in.problems.empty() ? "not a project" : in.problems.front().c_str(),
                    th::danger);
        return op;
    }

    g.set_font_size(th::sz_body);
    const std::string ident = in.project.name + "   entry " + in.project.entry +
                              "   schema " + std::to_string(in.project.schema);
    g.draw_text(area.x, y, ident.c_str(), th::text_dim);
    y += th::sz_body + th::space_md;

    // ---- the release id this source WOULD publish as -------------------------
    // Empty when the project is not shippable, and that is the honest answer: an
    // incomplete project has no package hash, not a provisional one.
    {
        ui.push_id("pkg");
        ui.begin_layout(ui::Rect{area.x, y, area.w, 28}, ui::Axis::X, ui::LayoutOpts{th::space_sm, 0});
        g.set_font_size(th::sz_caption);
        const ui::Rect lbl = ui.slot(64);
        g.draw_text(lbl.x, lbl.y + 8, "package", th::text_muted);
        g.set_font_size(th::sz_body);
        const ui::Rect val = ui.slot(150);
        g.draw_text(val.x, val.y + 6, in.package.empty() ? "—" : in.package.c_str(),
                    in.package.empty() ? th::text_muted : th::accent);
        if (ui.button(ui.slot(70), "Copy", false, !in.package.empty())) op = Op::CopyPackageHash;
        if (ui.button(ui.slot(90), "Re-inspect")) op = Op::Reinspect;
        ui.end_layout();
        ui.pop_id();
    }
    y += 28 + th::space_md;

    // ---- validation strip, ABOVE the browser ---------------------------------
    // Problems go where the eye already is. Putting them under a scrolling list is
    // how a warning becomes something you have to go and look for.
    const int problem_lines = in.problems.empty() ? 1 : static_cast<int>(in.problems.size());
    const int vh = th::space_sm * 2 + problem_lines * (th::sz_body + th::space_xs);
    g.fill_round_rect(area.x, y, area.w, vh, th::radius_md,
                      in.problems.empty() ? th::mix(th::success, th::elevated, 12)
                                          : th::mix(th::danger, th::elevated, 14));
    {
        int py = y + th::space_sm;
        g.set_font_size(th::sz_body);
        if (in.problems.empty()) {
            g.draw_text(area.x + th::space_md, py, "No problems — every declared asset resolves.",
                        th::success);
        } else {
            for (const auto& p : in.problems) {
                g.draw_text(area.x + th::space_md, py, ("• " + p).c_str(), th::danger);
                py += th::sz_body + th::space_xs;
            }
        }
    }
    y += vh + th::space_md;

    // ---- asset browser (left) + detail (right) -------------------------------
    const int detail_w = 240;
    const int list_w   = area.w - detail_w - th::space_md;
    const int body_h   = area.y + area.h - y;
    if (body_h < 60) return op;   // window too short to draw a list into honestly

    const int row_h = 26;
    const ui::Rect list_r{area.x, y, list_w, body_h};
    g.fill_round_rect(list_r.x, list_r.y, list_r.w, list_r.h, th::radius_md, th::elevated);

    if (in.assets.empty()) {
        g.set_font_size(th::sz_body);
        g.draw_text(list_r.x + th::space_md, list_r.y + th::space_md,
                    "No assets declared. Add  asset <type> <path>  lines to the manifest.",
                    th::text_muted);
    } else {
        ui.push_id("assets");
        const ui::Rect inner = ui.begin_scroll("assetlist", list_r, n * row_h + th::space_sm * 2);
        for (int i = 0; i < n; ++i) {
            const engine::InspectedAsset& a = in.assets[i];
            ui.push_id(i);
            const ui::Rect r{inner.x + th::space_sm, inner.y + th::space_sm + i * row_h,
                             inner.w - th::space_sm * 2, row_h - 2};
            // A missing asset is drawn IN PLACE, badged — not filtered out and not
            // sorted to the bottom. Where the hole is says as much as that it exists.
            if (ui.list_item(r, a.path.c_str(), i == selected, a.type.c_str(),
                             a.present ? "ok" : "MISSING",
                             a.present ? ui::Tone::Success : ui::Tone::Danger))
                selected = i;
            ui.pop_id();
        }
        ui.end_scroll();
        ui.pop_id();
    }

    // ---- the selected asset -------------------------------------------------
    const ui::Rect det{area.x + area.w - detail_w, y, detail_w, body_h};
    g.fill_round_rect(det.x, det.y, det.w, det.h, th::radius_md, th::elevated);
    if (!in.assets.empty() && selected >= 0 && selected < n) {
        const engine::InspectedAsset& a = in.assets[selected];
        const int fx = det.x + th::space_md, fw = det.w - th::space_md * 2;
        int fy = det.y + th::space_md;
        fy = field(g, fx, fy, fw, "PATH", a.path);
        fy = field(g, fx, fy, fw, "TYPE", a.type, th::text_dim);
        if (a.present) {
            fy = field(g, fx, fy, fw, "CONTENT HASH", engine::hash_hex(a.hash), th::accent);
            fy = field(g, fx, fy, fw, "SIZE", human_bytes(a.bytes), th::text_dim);
        } else {
            // No hash and no size, because there are no bytes. Printing "0" for a file
            // that is not there is the kind of confident wrong answer this panel exists
            // to avoid — it reads like an empty file rather than a missing one.
            fy = field(g, fx, fy, fw, "STATUS", "not found on disk", th::danger);
            g.set_font_size(th::sz_caption);
            g.draw_text(fx, fy, "Declared by the manifest but", th::text_muted);
            g.draw_text(fx, fy + th::sz_caption + 2, "absent under the asset root.", th::text_muted);
        }
    }
    return op;
}

} // namespace projectui
