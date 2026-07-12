// =============================================================================
//  baas/rbac/rbac.cc  —  see rbac.h
// =============================================================================
#include "baas/rbac/rbac.h"

#include <cctype>
#include <exception>

#include <sodium.h>

#include "baas/admin/audit.h"
#include "baas/auth/password.h"
#include "baas/db/db.h"

namespace web::rbac {

std::optional<Role> role_from_string(const std::string& s) {
    if (s == "viewer") return Role::Viewer;
    if (s == "admin")  return Role::Admin;
    if (s == "owner")  return Role::Owner;
    return std::nullopt;
}

std::string role_name(Role r) {
    switch (r) {
        case Role::Viewer: return "viewer";
        case Role::Admin:  return "admin";
        case Role::Owner:  return "owner";
    }
    return "viewer";
}

bool authorize(Role have, Role need) {
    return static_cast<int>(have) >= static_cast<int>(need);
}

bool valid_name(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'))
            return false;
    return true;
}

namespace {
// A random opaque operator key (prefix + hex), cryptographically random via libsodium.
std::string mint_key() {
    unsigned char buf[24];
    randombytes_buf(buf, sizeof buf);
    char hex[sizeof buf * 2 + 1];
    sodium_bin2hex(hex, sizeof hex, buf, sizeof buf);
    return std::string("opk_") + hex;
}
}  // namespace

std::optional<std::string> create_operator(long project_id, const std::string& name, Role role,
                                           const std::string& actor) {
    if (!valid_name(name)) return std::nullopt;
    const std::string key = mint_key();
    try {
        db::client()->execSqlSync(
            "INSERT INTO operators(project_id, name, key_hash, role) VALUES(?,?,?,?)",
            project_id, name, pw::hash(key), role_name(role));
    } catch (const std::exception&) {
        return std::nullopt;   // UNIQUE(project_id, name) violation → duplicate operator
    }
    audit::record(project_id, actor, "operator.create", "name=" + name + " role=" + role_name(role));
    return key;   // shown once
}

std::optional<Operator> authenticate(long project_id, const std::string& name,
                                     const std::string& key) {
    const auto rows = db::client()->execSqlSync(
        "SELECT key_hash, role FROM operators WHERE project_id=? AND name=?", project_id, name);
    if (rows.empty()) return std::nullopt;                                   // unknown operator
    if (!pw::verify(key, rows[0]["key_hash"].as<std::string>())) return std::nullopt;  // bad key
    const auto role = role_from_string(rows[0]["role"].as<std::string>());
    if (!role) return std::nullopt;   // corrupt role string (should never happen)
    return Operator{name, *role};
}

std::vector<Operator> list_operators(long project_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT name, role FROM operators WHERE project_id=? ORDER BY name ASC", project_id);
    std::vector<Operator> out;
    for (const auto& r : rows) {
        const auto role = role_from_string(r["role"].as<std::string>());
        out.push_back({r["name"].as<std::string>(), role.value_or(Role::Viewer)});
    }
    return out;
}

}  // namespace web::rbac
