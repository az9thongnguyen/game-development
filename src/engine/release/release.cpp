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

} // namespace engine
