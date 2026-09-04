// =============================================================================
//  engine/commands/registry.hpp  —  every operation, named and callable
// =============================================================================
//  The project's rule is that a GUI action and a CLI verb must be the same code,
//  not two implementations that agree today. `release_ops` already did that for
//  publish/promote/rollback by hand: main.cpp calls them, and so does the Hub scene.
//
//  A registry makes it structural rather than a convention. An operation registers
//  once under a stable id; `--cmd <id> [args]` runs it, a command palette lists it,
//  and a button calls the same `run()`. Adding an operation cannot accidentally add
//  it to only one of those.
//
//  PURE: no I/O of its own, no SDL. It holds handlers and calls them.
// =============================================================================
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "engine/release/ops.hpp"   // engine::OpResult is the shared outcome type

namespace cmd {

// What a command IS, separate from what it does — this is what a palette renders.
struct Info {
    std::string id;          // "<domain>.<verb>", e.g. "release.promote"
    std::string title;       // human label
    std::string hotkey;      // display string for a shortcut, may be empty
    std::string args_help;   // one-line argument summary, may be empty
};

// Arguments arrive as strings because they come from a command line, a palette entry
// box, or a dialog field — all of which are text. A handler validates its own.
using Handler = std::function<engine::OpResult(const std::vector<std::string>& args)>;

// Registering the same id twice REPLACES the handler rather than adding a second.
// Two handlers for one id is a bug that would otherwise surface as "sometimes the
// wrong thing happens", which is far worse than the last registration winning.
void register_command(Info info, Handler handler);

// Every registered command, in registration order (stable, so a palette does not
// reshuffle between runs).
const std::vector<Info>& all();

bool exists(std::string_view id);

// Run a command. An unknown id is a failed OpResult, not a crash and not silence:
// a mistyped id on a command line must say so.
engine::OpResult run(std::string_view id, const std::vector<std::string>& args = {});

// Drop every registration. For tests; a process registers once at startup.
void clear();

} // namespace cmd
