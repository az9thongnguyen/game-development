// =============================================================================
//  engine/commands/release_commands.cpp
// =============================================================================
#include "engine/commands/release_commands.hpp"

#include "engine/commands/registry.hpp"
#include "engine/project/inspect.hpp"
#include "engine/resource/resource.hpp"

namespace cmd {
namespace {

const std::string& arg(const std::vector<std::string>& a, std::size_t i) {
    static const std::string empty;
    return i < a.size() ? a[i] : empty;
}

// Every argument these commands take is required, AND non-empty. The reason in
// particular: the audit log exists to answer "why did this move", and an entry with
// a blank reason is worse than no entry at all — it looks like evidence and is not.
// The CLI used to default the reason to an empty string, which put exactly those
// blank lines into the log; the Studio already refuses to proceed without one, and
// this makes the two agree.
engine::OpResult need(const std::vector<std::string>& a, std::size_t n, const char* usage) {
    if (a.size() < n) return engine::OpResult{false, std::string("usage: ") + usage};
    for (std::size_t i = 0; i < n; ++i)
        if (a[i].empty()) return engine::OpResult{false, std::string("usage: ") + usage +
                                                         "   (no argument may be empty)"};
    return engine::OpResult{true, {}};
}

} // namespace

void register_release_commands(const std::vector<std::string>& known_entries) {
    // Inspect is a READ, so unlike the mutating commands it takes no reason and its
    // failure is a verdict rather than an error: "this project is not shippable, and
    // here is every reason" is the answer, not a malfunction. The CLI flag prints
    // this message; there is no second formatter.
    register_command(
        Info{"project.inspect", "Validate a project and list its content", "", "<project>"},
        [known_entries](const std::vector<std::string>& a) {
            if (auto e = need(a, 1, "project.inspect <project>"); !e.ok) return e;
            const engine::Inspection in = engine::inspect(a[0], known_entries);
            if (!in.parsed) return engine::OpResult{false, in.problems.front()};

            std::string msg = "project: " + in.path +
                              "\n  name   " + in.project.name +
                              "\n  schema " + std::to_string(in.project.schema) +
                              "\n  entry  " + in.project.entry;
            // Pad the type so the paths line up: a column you can scan is the whole
            // reason to list assets rather than count them.
            for (const auto& as : in.assets) {
                std::string type = as.present ? as.type : std::string("MISSING");
                if (type.size() < 8) type.append(8 - type.size(), ' ');
                msg += "\n  asset  " + type + " " + as.path;
                if (as.present) msg += "  [" + engine::hash_hex(as.hash) + "]";
            }
            if (in.problems.empty()) return engine::OpResult{true, msg + "\n  status OK"};
            msg += "\n  status " + std::to_string(in.problems.size()) + " problem(s):";
            for (const auto& e : in.problems) msg += "\n    - " + e;
            return engine::OpResult{false, msg};
        });

    register_command(
        Info{"project.publish", "Publish to a channel", "", "<project> <channel> <reason>"},
        [known_entries](const std::vector<std::string>& a) {
            if (auto e = need(a, 3, "project.publish <project> <channel> <reason>"); !e.ok) return e;
            return engine::publish(arg(a, 0), arg(a, 1), arg(a, 2), known_entries);
        });

    register_command(
        Info{"release.promote", "Promote a release forward", "", "<from> <to> <reason>"},
        [](const std::vector<std::string>& a) {
            if (auto e = need(a, 3, "release.promote <from> <to> <reason>"); !e.ok) return e;
            return engine::promote(arg(a, 0), arg(a, 1), arg(a, 2));
        });

    register_command(
        Info{"release.rollback", "Point a channel at a prior release", "", "<channel> <release-id> <reason>"},
        [](const std::vector<std::string>& a) {
            if (auto e = need(a, 3, "release.rollback <channel> <release-id> <reason>"); !e.ok) return e;
            return engine::rollback(arg(a, 0), arg(a, 1), arg(a, 2));
        });

    register_command(
        Info{"release.status", "Where each channel points", "", ""},
        [](const std::vector<std::string>&) {
            std::string msg;
            for (const auto& c : engine::status()) {
                if (!msg.empty()) msg += "\n";
                msg += c.name + " " + (c.release.empty() ? "unset"
                       : c.release + (c.present ? "  [present]" : "  [MISSING]"));
            }
            return engine::OpResult{true, msg};
        });

    register_command(
        Info{"release.log", "Audit history", "", "[channel]"},
        [](const std::vector<std::string>& a) {
            const auto recs = engine::log(arg(a, 0));
            if (recs.empty()) return engine::OpResult{true, "(no releases published yet)"};
            std::string msg;
            for (const auto& e : recs) {
                if (!msg.empty()) msg += "\n";
                msg += std::to_string(e.epoch) + " " + e.action + " " + e.channel + " " +
                       e.release + " <- " + (e.prev.empty() ? "(none)" : e.prev) +
                       (e.reason.empty() ? "" : "  # " + e.reason);
            }
            return engine::OpResult{true, msg};
        });
}

} // namespace cmd
