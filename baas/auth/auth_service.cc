// =============================================================================
//  baas/auth/auth_service.cc  —  see auth_service.h
// =============================================================================
#include "baas/auth/auth_service.h"

#include "baas/auth/password.h"
#include "baas/db/db.h"

namespace web::auth {

Result register_user(long project_id, const std::string& email,
                     const std::string& password, const std::string& display_name) {
    if (email.empty() || display_name.empty())
        return {std::nullopt,
                Error{400, "invalid_argument", "email and display_name are required"}};
    if (password.size() < 6)
        return {std::nullopt,
                Error{400, "weak_password", "password must be at least 6 characters"}};

    auto db = db::client();
    const auto dup = db::exec(db,
        "SELECT id FROM users WHERE project_id=? AND email=?", project_id, email);
    if (!dup.empty())
        return {std::nullopt, Error{409, "email_taken", "email already registered"}};

    const std::string ph = pw::hash(password);
    const long        id = static_cast<long>(db::insert_id(db,
        "INSERT INTO users(project_id, email, password_hash, display_name, is_guest) "
        "VALUES(?,?,?,?,0)",
        project_id, email, ph, display_name));

    return {User{id, display_name, false}, std::nullopt};
}

Result login(long project_id, const std::string& email, const std::string& password) {
    // One error object for both failure modes → no user enumeration.
    const Error bad{401, "invalid_credentials", "invalid email or password"};

    auto db = db::client();
    const auto rows = db::exec(db,
        "SELECT id, password_hash, display_name FROM users "
        "WHERE project_id=? AND email=?",
        project_id, email);
    if (rows.empty()) return {std::nullopt, bad};

    const std::string ph =
        rows[0]["password_hash"].isNull() ? "" : rows[0]["password_hash"].as<std::string>();
    if (ph.empty() || !pw::verify(password, ph)) return {std::nullopt, bad};

    return {User{rows[0]["id"].as<long>(), rows[0]["display_name"].as<std::string>(), false},
            std::nullopt};
}

Result guest(long project_id, const std::string& display_name, const std::string& device_id) {
    const std::string name = display_name.empty() ? "Guest" : display_name;
    auto              db   = db::client();

    // A known device gets its own guest back, name and all. Returning the STORED name
    // rather than the requested one matters: the account is the player's, and a later
    // launch should not silently rename them.
    if (!device_id.empty()) {
        const auto rows = db::exec(db,
            "SELECT id, display_name FROM users WHERE project_id=? AND device_id=?",
            project_id, device_id);
        if (!rows.empty())
            return {User{rows[0]["id"].as<long>(), rows[0]["display_name"].as<std::string>(), true},
                    std::nullopt};
    }

    const long id = static_cast<long>(
        device_id.empty()
            ? db::insert_id(db,
                  "INSERT INTO users(project_id, display_name, is_guest) VALUES(?,?,1)",
                  project_id, name)
            : db::insert_id(db,
                  "INSERT INTO users(project_id, display_name, is_guest, device_id) "
                  "VALUES(?,?,1,?)",
                  project_id, name, device_id));
    return {User{id, name, true}, std::nullopt};
}

}  // namespace web::auth
