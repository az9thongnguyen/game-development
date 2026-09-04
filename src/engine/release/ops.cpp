// =============================================================================
//  engine/release/ops.cpp  —  publish / promote / rollback (structured results)
// =============================================================================
#include "engine/release/ops.hpp"

#include <sstream>

#include <ctime>

#include "engine/assets.hpp"
#include "engine/project/inspect.hpp"
#include "engine/project/project.hpp"
#include "engine/release/release.hpp"
#include "engine/resource/resource.hpp"

namespace engine {

namespace {

// Atomic write: stage a .tmp then rename over the target (no torn release / channel).
bool write_atomic(const std::string& path, const std::string& text) {
    const std::string tmp = path + ".tmp";
    return assets::write_file(tmp, std::vector<uint8_t>(text.begin(), text.end())) &&
           assets::rename(tmp, path);
}

// Append one line to the append-only audit log.
void record_audit(const std::string& action, const std::string& channel,
                  const std::string& release, const std::string& prev, const std::string& reason) {
    AuditEntry e;
    e.epoch = static_cast<long long>(std::time(nullptr));
    e.action = action; e.channel = channel; e.release = release; e.prev = prev; e.reason = reason;
    const std::string line = audit_line(e);
    assets::append_file(audit_log_path(), std::vector<uint8_t>(line.begin(), line.end()));
}

// Every problem, joined into one message. publish() used to report only the FIRST
// missing asset, so a project with three broken paths took three runs to diagnose.
// The list is already ordered by how much each line explains (see inspect.hpp).
std::string problem_summary(const Inspection& in) {
    std::string msg;
    for (const auto& p : in.problems) {
        if (!msg.empty()) msg += "; ";
        msg += p;
    }
    return msg;
}

}  // namespace

std::optional<std::string> current_release(const std::string& channel) {
    auto bytes = assets::load_file(channel_path(channel));
    if (!bytes) return std::nullopt;
    return parse_channel(std::string(bytes->begin(), bytes->end()));
}

OpResult publish(const std::string& project_path, const std::string& channel,
                 const std::string& reason, const std::vector<std::string>& known_entries) {
    if (!valid_channel_name(channel)) return {false, "invalid channel name '" + channel + "'"};
    const Inspection in = inspect(project_path, known_entries);
    if (!in.shippable()) return {false, "not shippable: " + problem_summary(in)};

    const auto resources = in.resources();
    const std::string pkg  = build_package(in.project.name, in.project.schema,
                                           in.project.entry, resources);
    const std::string hex  = in.package;
    const std::string mpath = release_manifest_path(hex);
    const std::vector<uint8_t> pkg_bytes(pkg.begin(), pkg.end());

    bool verified = false;
    if (auto existing = assets::load_file(mpath)) {
        if (*existing != pkg_bytes) return {false, hex + " already stored with different bytes — refusing"};
        verified = true;
    } else if (!write_atomic(mpath, pkg)) {
        return {false, "cannot write " + mpath};
    }

    const std::string prev = current_release(channel).value_or("");
    if (!write_atomic(channel_path(channel), serialize_channel(hex)))
        return {false, "cannot update channel '" + channel + "'"};
    record_audit("publish", channel, hex, prev, reason);
    return {true, std::string(verified ? "verified " : "published ") + in.project.name +
                  " → " + channel + " " + hex};
}

OpResult promote(const std::string& from, const std::string& to, const std::string& reason) {
    if (!valid_channel_name(from) || !valid_channel_name(to)) return {false, "invalid channel name"};
    auto hex = current_release(from);
    if (!hex) return {false, "channel '" + from + "' is unset or malformed"};
    if (!assets::load_file(release_manifest_path(*hex)))
        return {false, "channel '" + from + "' points at missing release " + *hex};

    const std::string prev = current_release(to).value_or("");
    if (!write_atomic(channel_path(to), serialize_channel(*hex)))
        return {false, "cannot update channel '" + to + "'"};
    record_audit("promote", to, *hex, prev, reason);
    return {true, "promoted " + from + " → " + to + " (" + *hex + ")"};
}

OpResult rollback(const std::string& channel, const std::string& release_id, const std::string& reason) {
    if (!valid_channel_name(channel)) return {false, "invalid channel name '" + channel + "'"};
    if (!valid_hash_hex(release_id))  return {false, "invalid release id '" + release_id + "'"};
    if (!assets::load_file(release_manifest_path(release_id)))
        return {false, "no such release " + release_id};

    const std::string prev = current_release(channel).value_or("");
    if (!write_atomic(channel_path(channel), serialize_channel(release_id)))
        return {false, "cannot update channel '" + channel + "'"};
    record_audit("rollback", channel, release_id, prev, reason);
    return {true, "rolled back " + channel + " → " + release_id};
}


const std::vector<std::string>& well_known_channels() {
    static const std::vector<std::string> v{"development", "preview", "production"};
    return v;
}

std::vector<ChannelStatus> status() {
    std::vector<ChannelStatus> out;
    for (const std::string& ch : well_known_channels()) {
        ChannelStatus s;
        s.name = ch;
        if (auto hex = current_release(ch)) {
            s.release = *hex;
            s.present = assets::load_file(release_manifest_path(*hex)).has_value();
        }
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<AuditRecord> log(const std::string& channel_filter) {
    std::vector<AuditRecord> out;
    auto bytes = assets::load_file(audit_log_path());
    if (!bytes) return out;                    // nothing published yet is not an error
    std::istringstream in(std::string(bytes->begin(), bytes->end()));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto e = parse_audit_line(line);
        if (!e) continue;                      // a corrupt line must not hide the rest
        if (!channel_filter.empty() && e->channel != channel_filter) continue;
        out.push_back(AuditRecord{e->epoch, e->action, e->channel, e->release, e->prev, e->reason});
    }
    return out;
}

} // namespace engine
