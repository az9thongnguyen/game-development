// =============================================================================
//  engine/mix/mix.cpp
// =============================================================================
#include "engine/mix/mix.hpp"

#include <cstdio>
#include <sstream>

namespace mix {

namespace {

// One `rrggbb` or `rrggbbaa`. Returns false rather than a default: a mistyped colour
// that silently becomes black is the exact failure this format refuses to have.
bool parse_hex(const std::string& s, gfx::Color& out) {
    if (s.size() != 6 && s.size() != 8) return false;
    unsigned v = 0;
    for (char c : s) {
        unsigned d;
        if      (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a') + 10u;
        else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A') + 10u;
        else return false;
        v = (v << 4) | d;
    }
    out = s.size() == 6 ? gfx::rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)
                        : gfx::rgba((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return true;
}

std::string hex_of(gfx::Color c) {
    char b[12];
    if (gfx::a_of(c) == 255)
        std::snprintf(b, sizeof b, "%02x%02x%02x", gfx::r_of(c), gfx::g_of(c), gfx::b_of(c));
    else
        std::snprintf(b, sizeof b, "%02x%02x%02x%02x", gfx::r_of(c), gfx::g_of(c),
                      gfx::b_of(c), gfx::a_of(c));
    return b;
}

bool oops(std::string* why, int line, const std::string& msg) {
    if (why) *why = "line " + std::to_string(line) + ": " + msg;
    return false;
}

} // namespace

const Mix::Sheet* Mix::sheet(const std::string& n) const {
    for (const Sheet& s : sheets)
        if (s.name == n) return &s;
    return nullptr;
}

int tile_count(const gfx::Image& img, int tile) {
    // Only the divide-by-zero needs a guard. An image smaller than one tile already
    // answers 0 by integer division, and the extra `img.w < tile` test that used to be
    // here was arithmetic nothing could reach — the fifth redundant guard this project
    // has found by mutating a line and watching every test stay green (121, 122, 123,
    // 125, and now this).
    if (tile <= 0) return 0;
    return (img.w / tile) * (img.h / tile);
}

std::optional<Mix> parse_mix(const std::string& text, std::string* why) {
    Mix m;
    bool have_magic = false, have_size = false, have_name = false;
    std::istringstream in(text);
    std::string        line;
    int                n = 0;

    while (std::getline(in, line)) {
        ++n;
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ln(line);
        std::string        kind;
        if (!(ln >> kind)) continue;

        if (!have_magic) {
            // The magic must be the first thing that is not blank or a comment. A
            // format that accepts a file whose first record is `part` cannot tell a
            // mix from anything else that happens to have parts in it.
            if (kind != "mix1") { oops(why, n, "expected `mix1` as the first record"); return std::nullopt; }
            have_magic = true;
            continue;
        }

        if (kind == "mix1") { oops(why, n, "a second `mix1`"); return std::nullopt; }

        if (kind == "name") {
            if (have_name) { oops(why, n, "a second `name`"); return std::nullopt; }
            if (!(ln >> m.name)) { oops(why, n, "`name` needs a word"); return std::nullopt; }
            have_name = true;
        } else if (kind == "size") {
            if (have_size) { oops(why, n, "a second `size`"); return std::nullopt; }
            if (!(ln >> m.w >> m.h)) { oops(why, n, "`size` needs width and height"); return std::nullopt; }
            if (m.w <= 0 || m.h <= 0 || m.w > kMaxSide || m.h > kMaxSide) {
                oops(why, n, "`size` out of range (1.." + std::to_string(kMaxSide) + ")");
                return std::nullopt;
            }
            have_size = true;
        } else if (kind == "sheet") {
            Mix::Sheet s;
            if (!(ln >> s.name >> s.path)) { oops(why, n, "`sheet` needs a name and a path"); return std::nullopt; }
            if (!(ln >> s.tile)) s.tile = 16;
            if (s.tile <= 0 || s.tile > kMaxSide) { oops(why, n, "`sheet` tile size out of range"); return std::nullopt; }
            // Two sheets under one name means one of them loses and which one depends
            // on file order. That is a typo, not a redefinition.
            if (m.sheet(s.name) != nullptr) { oops(why, n, "sheet `" + s.name + "` declared twice"); return std::nullopt; }
            m.sheets.push_back(std::move(s));
        } else if (kind == "part") {
            Mix::Part p;
            std::string at;
            if (!(ln >> p.sheet >> p.index >> at >> p.x >> p.y) || at != "at") {
                oops(why, n, "`part <sheet> <index> at <x> <y>`");
                return std::nullopt;
            }
            if (p.index < 0) { oops(why, n, "a negative part index"); return std::nullopt; }
            if (m.sheet(p.sheet) == nullptr) {
                // The failure this format must not have silently: a typo in a sheet
                // name would otherwise read as "that part is missing art" and quietly
                // compose a sprite with a hole in it.
                oops(why, n, "part names sheet `" + p.sheet + "`, which was never declared");
                return std::nullopt;
            }
            m.parts.push_back(std::move(p));
        } else if (kind == "swap") {
            std::string from, to;
            Mix::Swap  s;
            if (!(ln >> from >> to)) { oops(why, n, "`swap <from> <to>`"); return std::nullopt; }
            if (!parse_hex(from, s.from) || !parse_hex(to, s.to)) {
                oops(why, n, "`swap` needs two rrggbb[aa] colours");
                return std::nullopt;
            }
            if (s.from == s.to) { oops(why, n, "a swap of a colour to itself"); return std::nullopt; }
            m.swaps.push_back(s);
        } else {
            oops(why, n, "unknown record `" + kind + "`");
            return std::nullopt;
        }
    }

    if (!have_magic) { oops(why, n, "not a mix file"); return std::nullopt; }
    if (!have_size)  { oops(why, n, "no `size`"); return std::nullopt; }
    if (m.parts.empty()) {
        oops(why, n, "no parts — a mix that composes nothing is a file somebody abandoned");
        return std::nullopt;
    }
    return m;
}

std::string to_text(const Mix& m) {
    std::string s = "mix1\n";
    if (!m.name.empty()) s += "name " + m.name + "\n";
    s += "size " + std::to_string(m.w) + " " + std::to_string(m.h) + "\n";
    for (const Mix::Sheet& sh : m.sheets)
        s += "sheet " + sh.name + " " + sh.path + " " + std::to_string(sh.tile) + "\n";
    for (const Mix::Part& p : m.parts)
        s += "part " + p.sheet + " " + std::to_string(p.index) + " at " +
             std::to_string(p.x) + " " + std::to_string(p.y) + "\n";
    for (const Mix::Swap& w : m.swaps)
        s += "swap " + hex_of(w.from) + " " + hex_of(w.to) + "\n";
    return s;
}

std::optional<gfx::Image> compose(
    const Mix& m,
    const std::function<const gfx::Image*(const std::string&)>& find,
    std::string* why) {
    gfx::Image out;
    out.w = m.w;
    out.h = m.h;
    out.pixels.assign(static_cast<std::size_t>(m.w) * m.h, 0);

    for (std::size_t i = 0; i < m.parts.size(); ++i) {
        const Mix::Part&  p  = m.parts[i];
        const Mix::Sheet* sh = m.sheet(p.sheet);
        if (sh == nullptr) { if (why) *why = "part " + std::to_string(i) + ": unknown sheet"; return std::nullopt; }
        const gfx::Image* img = find ? find(sh->name) : nullptr;
        if (img == nullptr) {
            if (why) *why = "sheet `" + sh->name + "` (" + sh->path + ") could not be read";
            return std::nullopt;
        }
        const int cols = img->w / sh->tile;
        const int count = tile_count(*img, sh->tile);
        if (count <= 0 || p.index >= count) {
            if (why) *why = "part " + std::to_string(i) + ": tile " + std::to_string(p.index) +
                            " is outside `" + sh->name + "` (" + std::to_string(count) + " tiles)";
            return std::nullopt;
        }
        const int sx = (p.index % cols) * sh->tile;
        const int sy = (p.index / cols) * sh->tile;

        // Entirely off the canvas is a typo. Partly off is a deliberate crop — a head
        // that hangs over the top edge is how you get a hat.
        if (p.x >= m.w || p.y >= m.h || p.x + sh->tile <= 0 || p.y + sh->tile <= 0) {
            if (why) *why = "part " + std::to_string(i) + ": lands entirely off the canvas";
            return std::nullopt;
        }

        for (int y = 0; y < sh->tile; ++y) {
            const int dy = p.y + y;
            if (dy < 0 || dy >= m.h) continue;
            for (int x = 0; x < sh->tile; ++x) {
                const int dx = p.x + x;
                if (dx < 0 || dx >= m.w) continue;
                const gfx::Color src =
                    img->pixels[static_cast<std::size_t>(sy + y) * img->w + (sx + x)];
                const unsigned a = gfx::a_of(src);
                if (a == 0) continue;
                gfx::Color& dst = out.pixels[static_cast<std::size_t>(dy) * m.w + dx];
                if (a == 255) { dst = src; continue; }
                // Alpha-over, integer. Composited onto what is already there, which is
                // why declaration order is the stacking order.
                const unsigned ia = 255u - a;
                const unsigned da = gfx::a_of(dst);
                const unsigned oa = a + da * ia / 255u;
                const auto     ch = [&](unsigned s, unsigned d) {
                    return oa == 0 ? 0u : (s * a + d * da * ia / 255u) / oa;
                };
                dst = gfx::rgba(ch(gfx::r_of(src), gfx::r_of(dst)),
                                ch(gfx::g_of(src), gfx::g_of(dst)),
                                ch(gfx::b_of(src), gfx::b_of(dst)), oa);
            }
        }
    }

    // Swaps last, and on the FINISHED canvas: a swap is about the sprite's palette,
    // not about one part's, so recolouring a shirt does not depend on which part drew
    // the pixel. Alpha is kept — a swap changes a colour, not a silhouette.
    for (const Mix::Swap& w : m.swaps)
        for (gfx::Color& c : out.pixels)
            if ((c & 0x00FFFFFFu) == (w.from & 0x00FFFFFFu))
                c = gfx::rgba(gfx::r_of(w.to), gfx::g_of(w.to), gfx::b_of(w.to), gfx::a_of(c));

    return out;
}

} // namespace mix
