// =============================================================================
//  games/farm/theme.cpp  —  see theme.hpp
// =============================================================================
#include "games/farm/theme.hpp"

#include <sstream>

namespace farm {

const Theme::Art* Theme::find(const std::string& layer, int id) const {
    const auto it = art.find({layer, id});
    return it == art.end() ? nullptr : &it->second;
}

std::optional<Theme> parse_theme(const std::string& text) {
    Theme t;
    std::istringstream in(text);
    std::string        line;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        std::istringstream ln(line);
        std::string        kind;
        if (!(ln >> kind)) continue;

        if (kind == "sheet") {
            std::string   name;
            Theme::Sheet  s;
            if (!(ln >> name >> s.path)) return std::nullopt;
            if (!(ln >> s.tile)) s.tile = 16;      // the size every pack in sight uses
            if (s.tile <= 0) return std::nullopt;
            // Two lines claiming one name means one of them silently loses, and which
            // one depends on file order. That is a typo, not a redefinition.
            if (!t.sheets.emplace(name, s).second) return std::nullopt;
        } else if (kind == "tile") {
            std::string layer;
            int         id = 0;
            Theme::Art  a;
            if (!(ln >> layer >> id >> a.sheet >> a.index)) return std::nullopt;
            // A negative index would be an index; 0 is the first tile of the sheet.
            if (id <= 0 || a.index < 0) return std::nullopt;
            if (t.sheets.find(a.sheet) == t.sheets.end()) return std::nullopt;
            t.art[{layer, id}] = a;
        } else {
            return std::nullopt;   // an unknown record here is a typo, not a future field
        }
    }
    if (t.sheets.empty()) return std::nullopt;
    return t;
}

} // namespace farm
