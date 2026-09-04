// =============================================================================
//  games/farm/theme.cpp  —  see theme.hpp
// =============================================================================
#include "games/farm/theme.hpp"

#include <sstream>

namespace farm {

int Theme::index_of(const std::string& layer, int id) const {
    const auto it = art.find({layer, id});
    return it == art.end() ? -1 : it->second;
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
            if (!(ln >> t.sheet)) return std::nullopt;
            if (!(ln >> t.tile)) t.tile = 16;      // the size every pack in sight uses
            if (t.tile <= 0) return std::nullopt;
        } else if (kind == "tile") {
            std::string layer;
            int         id = 0, index = 0;
            if (!(ln >> layer >> id >> index)) return std::nullopt;
            // A negative index would be an index; 0 is the first tile of the sheet.
            if (id <= 0 || index < 0) return std::nullopt;
            t.art[{layer, id}] = index;
        } else {
            return std::nullopt;   // an unknown record here is a typo, not a future field
        }
    }
    if (t.sheet.empty()) return std::nullopt;
    return t;
}

} // namespace farm
