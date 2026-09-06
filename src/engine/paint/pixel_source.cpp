// =============================================================================
//  engine/paint/pixel_source.cpp  —  see pixel_source.hpp
// =============================================================================
#include "engine/paint/pixel_source.hpp"

#include <map>
#include <sstream>
#include <vector>

namespace paint {
namespace {

std::string trim(std::string s) {
    const auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!s.empty() && ws(s.back())) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && ws(s[i])) ++i;
    return s.substr(i);
}

// `rrggbb` or `rrggbbaa`, BARE — no leading '#', because '#' opens a comment on the
// line above and a format cannot mean two things with one character. Six digits means
// opaque: typing the alpha for every solid colour is the kind of ceremony that gets
// copied wrong, and `00000000` is the only value where the alpha is the point.
bool parse_colour(const std::string& s, gfx::Color& out) {
    if (s.size() != 6 && s.size() != 8) return false;
    std::uint32_t v = 0;
    for (const char c : s) {
        int d;
        if      (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else return false;
        v = (v << 4) | static_cast<std::uint32_t>(d);
    }
    const std::uint8_t a = s.size() == 8 ? static_cast<std::uint8_t>(v & 0xFFu) : 255;
    if (s.size() == 8) v >>= 8;
    out = gfx::rgba(static_cast<std::uint8_t>((v >> 16) & 0xFFu),
                    static_cast<std::uint8_t>((v >> 8) & 0xFFu),
                    static_cast<std::uint8_t>(v & 0xFFu), a);
    return true;
}

} // namespace

std::optional<gfx::Image> bake_pixels(const std::string& text, std::string* why) {
    int                        size = 0, cols = 0, rows = 0;
    std::map<char, gfx::Color> palette;
    // (tile index) -> its `size` rows, kept until the whole file parses: a sheet is
    // only meaningful once every slot is accounted for, so nothing is composited
    // before the last line has been read.
    std::map<int, std::vector<std::string>> tiles;

    std::istringstream in(text);
    std::string        raw;
    int                lineno = 0;
    const auto fail = [&](const std::string& msg) -> std::optional<gfx::Image> {
        if (why) *why = "line " + std::to_string(lineno) + ": " + msg;
        return std::nullopt;
    };

    while (std::getline(in, raw)) {
        ++lineno;
        std::string line = raw;
        // '#' starts a comment, which is why it cannot also be a palette character —
        // a row using it would lose half of itself without a word.
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream ln(line);
        std::string        kind;
        ln >> kind;

        if (kind == "size") {
            if (size != 0) return fail("size declared twice");
            if (!(ln >> size) || size <= 0 || size > 256) return fail("size must be 1..256");
        } else if (kind == "grid") {
            if (cols != 0) return fail("grid declared twice");
            if (!(ln >> cols >> rows) || cols <= 0 || rows <= 0)
                return fail("grid must be <cols> <rows>, both positive");
        } else if (kind == "palette") {
            std::string ch, hex;
            if (!(ln >> ch >> hex)) return fail("palette must be <char> <rrggbb[aa]>");
            if (ch.size() != 1) return fail("a palette key is exactly one character");
            gfx::Color c = 0;
            if (!parse_colour(hex, c)) return fail("bad colour '" + hex + "'");
            // Two lines claiming one character means one of them silently loses, and
            // which one depends on file order. That is a typo, not a redefinition.
            if (!palette.emplace(ch[0], c).second)
                return fail(std::string("palette '") + ch[0] + "' declared twice");
        } else if (kind == "tile") {
            if (size == 0 || cols == 0) return fail("tile before size/grid");
            int idx = 0;
            if (!(ln >> idx)) return fail("tile must be <index>");
            if (idx < 0 || idx >= cols * rows)
                return fail("tile " + std::to_string(idx) + " is outside the " +
                            std::to_string(cols) + "x" + std::to_string(rows) + " grid");
            if (tiles.count(idx)) return fail("tile " + std::to_string(idx) + " drawn twice");

            std::vector<std::string> body;
            while (static_cast<int>(body.size()) < size && std::getline(in, raw)) {
                ++lineno;
                std::string row = raw;
                if (const auto hash = row.find('#'); hash != std::string::npos) row.erase(hash);
                row = trim(row);
                if (row.empty()) continue;      // blank lines may separate tiles
                if (static_cast<int>(row.size()) != size)
                    return fail("row is " + std::to_string(row.size()) + " characters, expected " +
                                std::to_string(size));
                for (const char c : row)
                    if (!palette.count(c))
                        return fail(std::string("'") + c + "' is not in the palette");
                body.push_back(row);
            }
            if (static_cast<int>(body.size()) != size)
                return fail("tile " + std::to_string(idx) + " ended after " +
                            std::to_string(body.size()) + " rows, expected " + std::to_string(size));
            tiles.emplace(idx, std::move(body));
        } else {
            return fail("unknown record '" + kind + "'");
        }
    }

    if (size == 0) return fail("no `size` line");
    if (cols == 0) return fail("no `grid` line");
    // A sheet with a gap is the failure this format exists to catch. An autotile set
    // resolves a piece per NEIGHBOURHOOD, so an undrawn slot is not "not yet" — it is
    // a hole that appears the first time the map changes shape, far from here.
    for (int i = 0; i < cols * rows; ++i)
        if (!tiles.count(i)) return fail("tile " + std::to_string(i) + " was never drawn");

    gfx::Image img;
    img.w = cols * size;
    img.h = rows * size;
    img.pixels.assign(static_cast<std::size_t>(img.w) * static_cast<std::size_t>(img.h), 0u);
    for (const auto& [idx, body] : tiles) {
        const int tx = (idx % cols) * size, ty = (idx / cols) * size;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                img.pixels[static_cast<std::size_t>(ty + y) * static_cast<std::size_t>(img.w) +
                           static_cast<std::size_t>(tx + x)] =
                    palette.find(body[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)])->second;
    }
    return img;
}


std::string blank_sheet(int size, int cols, int rows, const std::string& name, int max_px) {
    if (size < 1 || cols < 1 || rows < 1) return {};
    // Guard the multiplication before doing it: `size * cols` on ints the caller took
    // from a text field is where an overflow would come from, and an overflowed sheet
    // is a several-gigabyte allocation, not an error message.
    if (cols > max_px / size || rows > max_px / size) return {};

    std::string out;
    out += "# " + name + " — a new sheet, created in the Studio.\n";
    out += "# Every pixel is transparent; the shapes are yours. This file is the\n";
    out += "# SOURCE — `.hrt` is baked from it, and assets/ATTRIBUTION.md derives\n";
    out += "# \"drawn here\" from the fact that this file sits beside it.\n";
    out += "size " + std::to_string(size) + "\n";
    out += "grid " + std::to_string(cols) + " " + std::to_string(rows) + "\n";
    out += "palette . 00000000\n";

    const std::string row(static_cast<std::size_t>(size), '.');
    for (int t = 0; t < cols * rows; ++t) {
        out += "tile " + std::to_string(t) + "\n";
        for (int y = 0; y < size; ++y) out += row + "\n";
    }
    return out;
}

} // namespace paint
