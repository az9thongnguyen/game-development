// =============================================================================
//  baas/auth/auth_service.h  —  account logic (register / login / guest)
// =============================================================================
//  Pure-ish service over the DbClient, always scoped by project_id. Returns
//  either a User or a structured Error (mapped to the HTTP envelope by the
//  controller). No token issuance here — that is the controller's job.
// =============================================================================
#pragma once

#include <optional>
#include <string>

namespace web::auth {

struct User {
    long        id;
    std::string display_name;
    bool        is_guest;
};

struct Error {
    int         status;
    std::string code;
    std::string message;
};

struct Result {
    std::optional<User>  user;
    std::optional<Error> error;
};

// Create an email/password account. Rejects duplicates (409) and invalid input.
Result register_user(long project_id, const std::string& email,
                     const std::string& password, const std::string& display_name);

// Verify credentials. Same error for unknown-email and wrong-password (no
// user enumeration).
Result login(long project_id, const std::string& email, const std::string& password);

// Create an anonymous (guest) account so players can play before registering.
Result guest(long project_id, const std::string& display_name);

}  // namespace web::auth
