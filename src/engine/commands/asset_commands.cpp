// =============================================================================
//  engine/commands/asset_commands.cpp  —  see asset_commands.hpp
// =============================================================================
#include "engine/commands/asset_commands.hpp"

#include <string>

#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/image.hpp"
#include "engine/image_png.hpp"

namespace cmd {
namespace {

bool ends_with(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

} // namespace

void register_asset_commands() {
    register_command(
        {"asset.import", "Import a PNG as .hrt", "", "<src.png> <dst.hrt>"},
        [](const std::vector<std::string>& args) -> engine::OpResult {
            // D17: a mutating command refuses blank arguments. Importing writes a
            // file; an empty destination is not a default, it is a missing decision.
            if (args.size() < 2 || args[0].empty() || args[1].empty())
                return {false, "usage: asset.import <src.png> <dst.hrt>"};

            const std::string& src = args[0];
            const std::string& dst = args[1];
            // The extension is not decoration here: it is what tells the rest of the
            // project that this file is readable at runtime. Writing PNG bytes to a
            // path called .hrt would pass every later check and fail at load.
            if (!ends_with(dst, ".hrt"))
                return {false, "destination must end in .hrt (that is the format the engine reads)"};

            const auto bytes = assets::load_file(src);
            if (!bytes) return {false, "cannot read " + src};

            std::string why;
            const auto img = gfx::decode_png(*bytes, &why);
            if (!img) return {false, src + ": " + why};

            if (!assets::write_file(dst, gfx::encode_hrt(*img)))
                return {false, "cannot write " + dst};

            return {true, "imported " + src + " -> " + dst + "  (" + std::to_string(img->w) +
                              "x" + std::to_string(img->h) + ", " +
                              std::to_string(img->pixels.size() * 4 + 12) + " bytes)"};
        });
}

} // namespace cmd
