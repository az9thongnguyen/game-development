// =============================================================================
//  engine/release/ops.cpp  —  publish / promote / rollback (structured results)
// =============================================================================
#include "engine/release/ops.hpp"

#include <ctime>

#include "engine/assets.hpp"
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

// Resolve a project to (project, content-hashed resources), or a first-problem string.
// ponytail: a self-contained resolve local to the ops lib — the CLI (resolve_project) and
// hub (build_hub_view) keep their own small copies; unify the three if a fourth appears.
struct Res {
    Project                       proj;
    std::vector<PackagedResource> resources;
    std::string                   problem;   // empty ⇒ shippable
};
Res resolve(const std::string& path, const std::vector<std::string>& known) {
    Res r;
    auto bytes = assets::load_file(path);
    if (!bytes) { r.problem = "cannot read '" + path + "'"; return r; }
    auto p = parse_project(std::string(bytes->begin(), bytes->end()));
    if (!p) { r.problem = "'" + path + "' is not a valid gameproject1 manifest"; return r; }
    r.proj = *p;
    const auto errs = validate(*p, known);
    if (!errs.empty()) { r.problem = errs.front(); return r; }
    for (const auto& a : p->assets) {
        if (auto ab = assets::load_file(a.path))
            r.resources.push_back({a.type, a.path,
                                   content_hash(std::vector<uint8_t>(ab->begin(), ab->end()))});
        else { r.problem = "missing asset: " + a.path; return r; }
    }
    return r;
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
    Res r = resolve(project_path, known_entries);
    if (!r.problem.empty()) return {false, "not shippable: " + r.problem};

    const std::string pkg  = build_package(r.proj.name, r.proj.schema, r.proj.entry, r.resources);
    const std::string hex  = hash_hex(package_hash(r.resources));
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
    return {true, std::string(verified ? "verified " : "published ") + r.proj.name +
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

} // namespace engine
