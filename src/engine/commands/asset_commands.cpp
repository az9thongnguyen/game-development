// =============================================================================
//  engine/commands/asset_commands.cpp  —  see asset_commands.hpp
// =============================================================================
#include "engine/commands/asset_commands.hpp"

#include <string>

#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/image.hpp"
#include "engine/image_png.hpp"
#include "games/studio/recipe.hpp"
#include "games/studio/texture_gen.hpp"

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

    // The OTHER door into `.hrt`, and the reason there is only one format downstream.
    // `asset.import` brings a picture in from outside; this one bakes a picture this
    // project DREW. The Texture Lab already writes both the `.hrt` and a `.recipe`
    // sidecar next to it; until now nothing but the Lab's own Load button read the
    // recipe back, so the committed pixels were a claim. Now they are reproducible —
    // and a test can assert the file in the repo is what its recipe generates.
    register_command(
        {"asset.texture", "Bake a Texture Lab .recipe into .hrt", "", "<src.recipe> <dst.hrt>"},
        [](const std::vector<std::string>& args) -> engine::OpResult {
            if (args.size() < 2 || args[0].empty() || args[1].empty())
                return {false, "usage: asset.texture <src.recipe> <dst.hrt>"};

            const std::string& src = args[0];
            const std::string& dst = args[1];
            if (!ends_with(dst, ".hrt"))
                return {false, "destination must end in .hrt (that is the format the engine reads)"};

            const auto bytes = assets::load_file(src);
            if (!bytes) return {false, "cannot read " + src};

            // from_recipe cannot fail — every missing key keeps its default, which is
            // what makes the format forward-compatible. That tolerance is a trap for a
            // command that WRITES: an empty file would happily bake the default
            // texture over the destination. Recognising nothing means it is not one.
            int applied = 0;
            const studio::TextureParams p =
                studio::from_recipe(std::string(bytes->begin(), bytes->end()), &applied);
            if (applied == 0)
                return {false, src + ": no recipe keys recognised (is this a .recipe?)"};

            const gfx::Image img = studio::generate(p);
            if (!assets::write_file(dst, gfx::encode_hrt(img)))
                return {false, "cannot write " + dst};

            return {true, "baked " + src + " -> " + dst + "  (" + std::to_string(img.w) + "x" +
                              std::to_string(img.h) + ", " + std::to_string(applied) + " keys)"};
        });
}

} // namespace cmd
