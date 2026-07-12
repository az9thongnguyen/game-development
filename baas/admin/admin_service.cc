// =============================================================================
//  baas/admin/admin_service.cc  —  see admin_service.h
// =============================================================================
#include "baas/admin/admin_service.h"

#include <cstddef>

#include <sodium.h>

#include "baas/admin/audit.h"
#include "baas/auth/password.h"
#include "baas/db/db.h"

namespace web::admin {
namespace {

// A random opaque token, prefix + hex(nbytes) — cryptographically random via libsodium.
std::string rand_token(const char* prefix, std::size_t nbytes) {
    std::vector<unsigned char> buf(nbytes);
    randombytes_buf(buf.data(), nbytes);
    std::string hex(nbytes * 2 + 1, '\0');
    sodium_bin2hex(hex.data(), hex.size(), buf.data(), nbytes);
    hex.resize(nbytes * 2);
    return std::string(prefix) + hex;
}

}  // namespace

NewProject create_project(const std::string& name) {
    const std::string pub = rand_token("pk_", 8);
    const std::string sec = rand_token("sk_", 16);
    const auto        ins = db::client()->execSqlSync(
        "INSERT INTO projects(name, public_key, secret_key_hash) VALUES(?,?,?)",
        name, pub, pw::hash(sec));   // secret stored hashed; plaintext returned once
    const long id = static_cast<long>(ins.insertId());
    audit::record(id, "admin", "project.create", name);
    return {id, name, pub, sec};
}

std::string rotate_secret(long project_id) {
    const std::string sec = rand_token("sk_", 16);
    db::client()->execSqlSync("UPDATE projects SET secret_key_hash=? WHERE id=?",
                              pw::hash(sec), project_id);
    audit::record(project_id, "admin", "secret.rotate", "project secret rotated");
    return sec;   // returned once; the old secret no longer verifies
}

std::vector<ProjectInfo> list_projects() {
    const auto rows =
        db::client()->execSqlSync("SELECT id, name, public_key FROM projects ORDER BY id ASC");
    std::vector<ProjectInfo> out;
    for (const auto& r : rows)
        out.push_back({r["id"].as<long>(), r["name"].as<std::string>(),
                       r["public_key"].as<std::string>()});
    return out;
}

std::vector<UserInfo> list_users(long project_id) {
    const auto rows = db::client()->execSqlSync(
        "SELECT id, email, display_name, is_guest FROM users WHERE project_id=? ORDER BY id ASC",
        project_id);
    std::vector<UserInfo> out;
    for (const auto& r : rows)
        out.push_back({r["id"].as<long>(),
                       r["email"].isNull() ? std::string() : r["email"].as<std::string>(),
                       r["display_name"].as<std::string>(), r["is_guest"].as<int>() != 0});
    return out;
}

}  // namespace web::admin
