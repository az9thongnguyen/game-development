// =============================================================================
//  engine/commands/asset_commands.cpp  —  see asset_commands.hpp
// =============================================================================
#include "engine/commands/asset_commands.hpp"

#include <map>
#include <string>

#include "engine/asset/provenance.hpp"
#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "engine/image.hpp"
#include "engine/image_png.hpp"
#include "engine/mix/mix.hpp"
#include "engine/paint/pixel_source.hpp"
#include "engine/project/project.hpp"
#include "engine/tilemap/map2.hpp"
#include "games/studio/recipe.hpp"
#include "games/studio/texture_gen.hpp"

namespace cmd {
namespace {

bool ends_with(const std::string& s, const std::string& tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// A new asset's name becomes a PATH, and a path assembled from text a user typed is
// where directory traversal lives. Allow exactly what a filename needs and nothing
// that can leave the folder: no `/`, no `\`, no `.` (which rules out `..` without
// having to reason about it), no empty name.
bool safe_stem(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char ch : s) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!ok) return false;
    }
    return true;
}

std::vector<std::uint8_t> bytes_of(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
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

    // The THIRD door, and the one a road needed. The other two bring pixels in from
    // outside or compute them from twelve numbers; neither can place a corner piece.
    // This one bakes an ASCII sheet a person wrote — the source stays in the repo,
    // diffable, so a sixteen-piece set can be reviewed as the one design it is
    // rather than as sixteen unrelated images. See pixel_source.hpp.
    register_command(
        {"asset.pixels", "Bake an ASCII pixel-art sheet into .hrt", "", "<src.pix> <dst.hrt>"},
        [](const std::vector<std::string>& args) -> engine::OpResult {
            if (args.size() < 2 || args[0].empty() || args[1].empty())
                return {false, "usage: asset.pixels <src.pix> <dst.hrt>"};

            const std::string& src = args[0];
            const std::string& dst = args[1];
            if (!ends_with(dst, ".hrt"))
                return {false, "destination must end in .hrt (that is the format the engine reads)"};

            const auto bytes = assets::load_file(src);
            if (!bytes) return {false, "cannot read " + src};

            std::string why;
            const auto img = paint::bake_pixels(std::string(bytes->begin(), bytes->end()), &why);
            if (!img) return {false, src + ": " + why};

            if (!assets::write_file(dst, gfx::encode_hrt(*img)))
                return {false, "cannot write " + dst};

            return {true, "drew " + src + " -> " + dst + "  (" + std::to_string(img->w) + "x" +
                              std::to_string(img->h) + ")"};
        });

    // ---- the FOURTH door (chapter 135) ------------------------------------------
    // Assemble a sprite from parts. The other three doors each answer "where did this
    // picture come from"; this one answers a question none of them can — make me a
    // hundred sprites that all belong together. Held to exactly the same standard: the
    // `.mix` is a SOURCE (it stays out of the manifest), `provenance_core` derives
    // `mixed` from it, and a test re-bakes it and compares bytes.
    cmd::register_command(
        {"asset.mix", "Compose a sprite from parts into .hrt", "", "<src.mix> <dst.hrt>"},
        [](const std::vector<std::string>& args) -> engine::OpResult {
            if (args.size() < 2 || args[0].empty() || args[1].empty())
                return {false, "usage: asset.mix <src.mix> <dst.hrt>"};

            const std::string& src = args[0];
            const std::string& dst = args[1];
            if (!ends_with(dst, ".hrt"))
                return {false, "destination must end in .hrt (that is the format the engine reads)"};

            const auto bytes = assets::load_file(src);
            if (!bytes) return {false, "cannot read " + src};

            std::string why;
            const auto  m = mix::parse_mix(std::string(bytes->begin(), bytes->end()), &why);
            if (!m) return {false, src + ": " + why};

            // The I/O the pure core refuses to do. Each sheet is read once however
            // many parts come out of it — a mixer's whole shape is many parts, few
            // sheets, and re-reading per part would make that the expensive case.
            std::map<std::string, gfx::Image> loaded;
            for (const mix::Mix::Sheet& sh : m->sheets) {
                auto img = gfx::load_image(sh.path);
                if (!img) return {false, src + ": cannot read sheet '" + sh.name + "' (" + sh.path + ")"};
                loaded.emplace(sh.name, std::move(*img));
            }
            const auto find = [&loaded](const std::string& n) -> const gfx::Image* {
                const auto it = loaded.find(n);
                return it == loaded.end() ? nullptr : &it->second;
            };

            const auto img = mix::compose(*m, find, &why);
            if (!img) return {false, src + ": " + why};

            if (!assets::write_file(dst, gfx::encode_hrt(*img)))
                return {false, "cannot write " + dst};

            return {true, "mixed " + std::to_string(m->parts.size()) + " part(s) -> " + dst +
                              "  (" + std::to_string(img->w) + "x" + std::to_string(img->h) + ")"};
        });

    // The FIFTH verb on this file, and the only one that writes no pixels. The other
    // four each open a door into `.hrt`; this one walks the tree afterwards and
    // writes down what came through which. CLAUDE.md has always carried the rule
    // ("every new .hrt gains an ATTRIBUTION line in the same change") and never
    // carried the check, which is how twenty files came to be covered by a paragraph
    // rather than by a line.
    //
    // It REFUSES when the ledger is not ok — an unrecorded file, a stale claim, a
    // file with two origins. Writing the table anyway would put the word UNRECORDED
    // in a committed file and call it done; exiting non-zero is what makes `ctest`
    // and CI able to disagree with you.
    register_command(
        {"asset.attribution", "Re-bake the provenance ledger inside ATTRIBUTION.md", "",
         "<doc.md>"},
        [](const std::vector<std::string>& args) -> engine::OpResult {
            if (args.empty() || args[0].empty())
                return {false, "usage: asset.attribution <doc.md>"};
            const std::string& doc_path = args[0];

            const engine::Ledger l = engine::scan_provenance();
            if (l.files.empty())
                return {false, "no .hrt found under the asset root — wrong working directory?"};

            const auto before = assets::load_file(doc_path);
            if (!before) return {false, "cannot read " + doc_path};

            auto after = engine::splice_ledger(std::string(before->begin(), before->end()),
                                               engine::ledger_markdown(l));
            if (!after)
                return {false, doc_path + " has no generated-ledger markers; expected " +
                                   std::string(engine::kLedgerBegin) + " ... " +
                                   std::string(engine::kLedgerEnd)};

            if (!assets::write_file(doc_path, std::vector<std::uint8_t>(after->begin(), after->end())))
                return {false, "cannot write " + doc_path};

            std::string msg = "ledger: " + std::to_string(l.files.size()) + " assets";
            if (!l.ok()) {
                msg += ", " + std::to_string(l.unrecorded()) + " unrecorded";
                for (const auto& p : l.problems) msg += "\n  problem: " + p;
            }
            return {l.ok(), msg};
        });

    // The one-way door out of `fpsmap1`. The shared reader has migrated it on the way
    // in since chapter 110, which was enough to READ old files and not enough to stop
    // making new ones — Map Lab kept writing the old format for ten more chapters. A
    // verb makes the conversion a thing you DO once to a file, after which the old
    // format has no producers and no committed consumers left.
    register_command(
        {"map.migrate", "Convert an fpsmap1 level to map2", "", "<src.map> <dst.map2>"},
        [](const std::vector<std::string>& args) -> engine::OpResult {
            if (args.size() < 2 || args[0].empty() || args[1].empty())
                return {false, "usage: map.migrate <src.map> <dst.map2>"};
            const auto bytes = assets::load_file(args[0]);
            if (!bytes) return {false, "cannot read " + args[0]};
            const std::string text(bytes->begin(), bytes->end());

            // from_fpsmap1, not load(): pointed at a map2 file, `load` would succeed
            // and this would report a migration that did not happen. Refusing names
            // the situation instead.
            auto m = tilemap::from_fpsmap1(text);
            if (!m) return {false, args[0] + " is not an fpsmap1 level (already map2?)"};

            const std::string out = tilemap::to_text(*m);
            if (!assets::write_file(args[1], bytes_of(out)))
                return {false, "cannot write " + args[1]};
            return {true, "migrated " + args[0] + " -> " + args[1] + "  (" +
                              std::to_string(m->w) + "x" + std::to_string(m->h) + ", " +
                              std::to_string(m->layers.size()) + " layers, " +
                              std::to_string(m->entities.size()) + " entities)"};
        });

    // Bringing an asset INTO EXISTENCE, which until now happened in a text editor.
    // Chapter 127 wrote the ceiling down: the Pixels workspace edits the textures a
    // manifest already declares, so the Studio could change art and not add any.
    //
    // It writes the `.pix` FIRST and bakes from it, rather than writing a blank
    // `.hrt`. That is the whole design: a file born as a source is `drawn` in the
    // ledger from its first second, and the attribution rule needs no special case
    // for "the Studio made this one".
    register_command(
        {"asset.new", "Create a new drawable sheet: .pix source, baked .hrt, declared", "",
         "<name> <tile-px> <cols> <rows> [<project.gameproject>]"},
        [](const std::vector<std::string>& args) -> engine::OpResult {
            if (args.size() < 4)
                return {false, "usage: asset.new <name> <tile-px> <cols> <rows> [<project>]"};
            const std::string& name = args[0];
            if (!safe_stem(name))
                return {false, "name must be letters, digits, _ or - (it becomes a filename): '" +
                                   name + "'"};

            int size = 0, cols = 0, rows = 0;
            try {
                size = std::stoi(args[1]);
                cols = std::stoi(args[2]);
                rows = std::stoi(args[3]);
            } catch (...) {
                return {false, "tile-px, cols and rows must be whole numbers"};
            }

            const std::string src = "textures/" + name + ".pix";
            const std::string dst = "textures/" + name + ".hrt";

            // Never over an existing file. Creating is not editing, and a `.hrt` this
            // silently replaced would take an asset's history with it.
            if (assets::load_file(src)) return {false, src + " already exists"};
            if (assets::load_file(dst)) return {false, dst + " already exists"};

            const std::string sheet = paint::blank_sheet(size, cols, rows, name);
            if (sheet.empty())
                return {false, "refusing a sheet of " + args[1] + "px x " + args[2] + " x " +
                                   args[3] + " — every dimension must be >= 1 and the sheet "
                                   "at most 4096px a side"};

            std::string why;
            const auto img = paint::bake_pixels(sheet, &why);
            if (!img) return {false, "the sheet this would create does not parse: " + why};

            // Everything that can be REFUSED is refused before anything is WRITTEN.
            // The first version validated the manifest after creating the files, so
            // `asset.new x 16 1 1 nosuch.gameproject` reported failure and left two
            // real files behind plus a stale ledger — a failed creation that half
            // happened is worse than one that did not, and it is the same lesson the
            // release store learned as stage-then-rename.
            const bool          declare = args.size() >= 5 && !args[4].empty();
            engine::Project     proj;
            bool                already = false;
            if (declare) {
                const auto pb = assets::load_file(args[4]);
                if (!pb) return {false, "cannot read " + args[4] + " (nothing was created)"};
                auto parsed = engine::parse_project(std::string(pb->begin(), pb->end()));
                if (!parsed)
                    return {false, args[4] + " is not a manifest (nothing was created)"};
                proj = *parsed;
                for (const auto& a : proj.assets)
                    if (a.path == dst) already = true;
                if (!already) proj.assets.push_back({"texture", dst});
            }

            if (!assets::write_file(src, bytes_of(sheet))) return {false, "cannot write " + src};
            if (!assets::write_file(dst, gfx::encode_hrt(*img))) return {false, "cannot write " + dst};

            std::string msg = "created " + src + " -> " + dst + "  (" +
                              std::to_string(img->w) + "x" + std::to_string(img->h) + ")";

            // Declared, or it is a file the project cannot see. An asset the Studio
            // made and did not register is exactly the "created but invisible" state
            // this command exists to remove.
            if (declare) {
                if (!already && !assets::write_file(args[4], bytes_of(engine::to_text(proj))))
                    return {false, msg + "; but cannot write " + args[4]};
                msg += already ? "; already declared in " + args[4]
                               : "; declared in " + args[4];
            }

            // And the ledger, so nobody has to remember. Absent doc = said out loud,
            // not skipped quietly: this is the rule the slice exists to automate.
            if (assets::load_file("ATTRIBUTION.md")) {
                const engine::OpResult led = cmd::run("asset.attribution", {"ATTRIBUTION.md"});
                msg += led.ok ? "; ledger re-baked" : "; LEDGER FAILED: " + led.message;
                if (!led.ok) return {false, msg};
            } else {
                msg += "; no ATTRIBUTION.md at the asset root, ledger not written";
            }
            return {true, msg};
        });
}

} // namespace cmd
