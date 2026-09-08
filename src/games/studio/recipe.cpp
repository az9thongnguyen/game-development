// =============================================================================
//  games/studio/recipe.cpp
// =============================================================================
#include "games/studio/recipe.hpp"

#include <algorithm>
#include <sstream>

namespace studio {

std::string to_recipe(const TextureParams& p) {
    std::ostringstream o;
    o << "seed="       << p.seed                          << '\n'
      << "size="       << p.size                          << '\n'
      << "base="       << int(p.base)                     << '\n'
      << "basis="      << int(p.basis)                    << '\n'
      << "frequency="  << p.frequency                     << '\n'
      << "octaves="    << p.octaves                       << '\n'
      << "gain="       << p.gain                          << '\n'
      << "lacunarity=" << p.lacunarity                    << '\n'
      << "lo="         << static_cast<unsigned long>(p.lo) << '\n'
      << "hi="         << static_cast<unsigned long>(p.hi) << '\n'
      << "op="         << int(p.op)                       << '\n'
      << "op_amount="  << p.op_amount                     << '\n';
    return o.str();
}

std::string to_recipe(const TextureRecipe& recipe) {
    return to_recipe(recipe.texture) + "frames=" + std::to_string(recipe.frames) + '\n';
}

namespace {

TextureRecipe parse(const std::string& text, int* applied, bool include_frames) {
    int n = 0;
    TextureRecipe recipe;
    TextureParams& p = recipe.texture;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        try {
            ++n;
            if      (key == "seed")       p.seed       = std::uint32_t(std::stoul(val));
            else if (key == "size")       p.size       = std::stoi(val);
            else if (key == "base")       p.base       = TextureParams::Base(std::stoi(val));
            else if (key == "basis")      p.basis      = Basis(std::stoi(val));
            else if (key == "frequency")  p.frequency  = std::stoi(val);
            else if (key == "octaves")    p.octaves    = std::stoi(val);
            else if (key == "gain")       p.gain       = std::stod(val);
            else if (key == "lacunarity") p.lacunarity = std::stod(val);
            else if (key == "lo")         p.lo         = gfx::Color(std::stoul(val));
            else if (key == "hi")         p.hi         = gfx::Color(std::stoul(val));
            else if (key == "op")         p.op         = TextureParams::Op(std::stoi(val));
            else if (key == "op_amount")  p.op_amount  = std::stod(val);
            else if (include_frames && key == "frames")
                recipe.frames = std::clamp(std::stoi(val), 1, 64);
            else
                --n;
        } catch (...) {
            --n;
        }
    }
    if (applied) *applied = n;
    return recipe;
}

} // namespace

TextureParams from_recipe(const std::string& text, int* applied) {
    return parse(text, applied, false).texture;
}

TextureRecipe parse_recipe(const std::string& text, int* applied) {
    return parse(text, applied, true);
}

} // namespace studio
