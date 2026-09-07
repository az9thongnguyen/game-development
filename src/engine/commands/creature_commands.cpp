// =============================================================================
//  engine/commands/creature_commands.cpp  —  verify a recorded battle
// =============================================================================
//  A game's operation, registered in the engine's one registry rather than behind a
//  flag of its own. That is the whole point of chapter 122: an operation exists once
//  and `--cmd`, the palette and a button all reach the same code. A verifier that
//  lived only in `main.cpp` would be a second door, and a second door is how the
//  Studio ends up unable to do something the command line can.
//
//  What it is FOR: CI on Linux/x86_64/gcc re-plays a battle recorded on
//  macOS/arm64/clang and checks every turn hash. Until this existed, "the same
//  actions produce the same battle on every machine" was proved by a test that ran
//  a thousand battles inside ONE process — which proves the code is a pure function,
//  not that two toolchains agree about what it computes.
// =============================================================================
#include "engine/commands/creature_commands.hpp"

#include <string>
#include <vector>

#include "engine/assets.hpp"
#include "engine/commands/registry.hpp"
#include "games/creatures/defs.hpp"
#include "games/creatures/replay.hpp"

namespace cmd {
namespace {

bool read_asset(const char* path, std::string& out) {
    const auto bytes = assets::load_file(path);
    if (!bytes) return false;
    out.assign(bytes->begin(), bytes->end());
    return true;
}

} // namespace

void register_creature_commands() {
    register_command(
        {"creature.verify", "Re-play a recorded battle and check every turn", "",
         "<file.crep>"},
        [](const std::vector<std::string>& args) -> engine::OpResult {
            if (args.empty() || args[0].empty())
                return {false, "usage: creature.verify <file.crep>"};

            creature::Dex dex;
            std::string   why;
            if (!creature::load_dex(dex, read_asset, &why)) return {false, why};

            const auto bytes = assets::load_file(args[0]);
            if (!bytes) return {false, "cannot read " + args[0]};

            creature::Replay r;
            if (!creature::read_replay(dex, std::string(bytes->begin(), bytes->end()), r, &why))
                return {false, args[0] + ": " + why};

            const creature::Verdict v = creature::verify(dex, r);
            if (v.ok)
                return {true, args[0] + ": OK — " + v.why + ", winner " +
                                  std::to_string(v.final.winner)};

            // The two failures are reported apart because they are different jobs.
            // "the rules moved" is somebody's balance commit; "desync" is two
            // machines disagreeing about integer arithmetic, which is the one this
            // command was written to catch.
            const char* kind =
                v.fault == creature::Verdict::Fault::Rules ? "RULES MOVED" : "DESYNC";
            return {false, args[0] + ": " + kind + " — " + v.why};
        });

    register_command(
        {"creature.record", "Write the reference battle (re-bake the committed .crep)", "",
         "<dst.crep>"},
        [](const std::vector<std::string>& args) -> engine::OpResult {
            if (args.empty() || args[0].empty())
                return {false, "usage: creature.record <dst.crep>"};

            creature::Dex dex;
            std::string   why;
            if (!creature::load_dex(dex, read_asset, &why)) return {false, why};

            const creature::Replay r    = creature::reference_battle(dex);
            const std::string      text = creature::write_replay(r, &why);
            if (text.empty()) return {false, why};
            if (!assets::write_file(args[0],
                                    std::vector<std::uint8_t>(text.begin(), text.end())))
                return {false, "cannot write " + args[0]};

            return {true, "recorded " + args[0] + "  (" + std::to_string(r.turns.size()) +
                              " turns, winner " + std::to_string(creature::play(dex, r).winner) +
                              ")"};
        });
}

} // namespace cmd
