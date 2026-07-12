// =============================================================================
//  engine/release/release.cpp  —  release-store paths, channel format, validators
// =============================================================================
#include "engine/release/release.hpp"

#include <cctype>
#include <sstream>

namespace engine {

bool valid_hash_hex(const std::string& hex) {
    if (hex.size() != 16) return false;
    for (char c : hex)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;  // lowercase hex only
    return true;
}

bool valid_channel_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) return false;
    return true;   // no '/', no '.', no whitespace → cannot escape the store dir
}

std::string release_dir(const std::string& hash_hex)           { return "releases/" + hash_hex; }
std::string release_manifest_path(const std::string& hash_hex) { return release_dir(hash_hex) + "/package.txt"; }
std::string channel_path(const std::string& name)              { return "channels/" + name; }

std::string serialize_channel(const std::string& hash_hex) { return "channel1 " + hash_hex + "\n"; }

std::optional<std::string> parse_channel(const std::string& text) {
    std::istringstream in(text);
    std::string magic, hex;
    if (!(in >> magic >> hex)) return std::nullopt;   // empty / single token → malformed
    if (magic != "channel1")   return std::nullopt;   // fail closed on unknown/absent magic
    if (!valid_hash_hex(hex))  return std::nullopt;    // guard: a channel must point at a well-formed id
    return hex;
}

std::string audit_log_path() { return "releases/audit.log"; }

std::string audit_line(const AuditEntry& e) {
    // reason is last so it may contain spaces; predecessor "-" means "was unset".
    return std::to_string(e.epoch) + " " + e.action + " " + e.channel + " " +
           e.release + " " + (e.prev.empty() ? "-" : e.prev) +
           (e.reason.empty() ? "" : " " + e.reason) + "\n";
}

std::optional<AuditEntry> parse_audit_line(const std::string& line) {
    std::istringstream in(line);
    AuditEntry e;
    std::string prev;
    if (!(in >> e.epoch >> e.action >> e.channel >> e.release >> prev)) return std::nullopt;
    if (!valid_hash_hex(e.release)) return std::nullopt;               // release is always a real id
    if (prev != "-" && !valid_hash_hex(prev)) return std::nullopt;      // predecessor is "-" or a real id
    e.prev = (prev == "-") ? "" : prev;
    std::getline(in, e.reason);
    if (!e.reason.empty() && e.reason.front() == ' ') e.reason.erase(0, 1);  // drop the single separator space
    return e;
}

} // namespace engine
